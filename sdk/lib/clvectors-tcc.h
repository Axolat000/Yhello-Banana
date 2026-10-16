/* TinyCC extension used ONLY by the OpenCL compiler copy. Vector unions have
 * a reserved tag so their normal C layout/ABI can be reused; operations are
 * lowered to scalar lanes, without requiring SIMD support in the backend. */
static int bcl_vec_info(CType* type, CType* elem) {
    const char* name;
    int n;
    if ((type->t & VT_BTYPE) != VT_STRUCT || !type->ref) return 0;
    name = get_tok_str(type->ref->v & ~SYM_STRUCT, NULL);
    if (!name || strncmp(name, "__bcl_vec_", 10)) return 0;
    name += 10;
    elem->ref = NULL;
    if (!strncmp(name,"float",5)) { elem->t = VT_FLOAT; name += 5; }
    else if (!strncmp(name,"uint",4)) { elem->t = VT_INT | VT_UNSIGNED; name += 4; }
    else if (!strncmp(name,"int",3)) { elem->t = VT_INT; name += 3; }
    else return 0;
    n = atoi(name);
    return n == 2 || n == 3 || n == 4 || n == 8 || n == 16 ? n : 0;
}

static CType bcl_vec_type(int n, int scalar) {
    char name[32];
    Sym* sym;
    sprintf(name, "%s%d", (scalar & VT_BTYPE) == VT_FLOAT ? "float" : (scalar & VT_UNSIGNED) ? "uint" : "int", n);
    sym = sym_find(tok_alloc_const(name));
    if (!sym) tcc_error("missing OpenCL vector type %s", name);
    return sym->type;
}

/* Materialize an address into a local pointer slot so repeated lane access
 * cannot reuse a register clobbered while lowering a different lane. */
static SValue bcl_vec_save(SValue value) {
    SValue result;
    CType type = value.type;
    int offset;
    vpushv(&value);
    if ((type.t & VT_BTYPE) == VT_STRUCT) {
        gaddrof(); vtop->type = char_pointer_type;
        loc = (loc - PTR_SIZE) & -PTR_SIZE;
        offset = loc;
        vset(&char_pointer_type, VT_LOCAL | VT_LVAL, offset);
        vswap(); vstore(); vpop();
        vset(&type, VT_LLOCAL | VT_LVAL, offset);
    } else {
        int align, size = type_size(&type, &align);
        loc = (loc - size) & -align; offset = loc;
        vset(&type, VT_LOCAL | VT_LVAL, offset);
        vswap(); vstore(); vpop();
        vset(&type, VT_LOCAL | VT_LVAL, offset);
    }
    result = *vtop; vtop--;
    return result;
}

static SValue bcl_vec_temp(CType type) {
    SValue result;
    int align, size = type_size(&type, &align);
    loc = (loc - size) & -align;
    vset(&type, VT_LOCAL | VT_LVAL, loc);
    result = *vtop; vtop--;
    return result;
}

static void bcl_vec_lane(SValue* value, int lane) {
    CType elem;
    vpushv(value);
    if (bcl_vec_info(&value->type, &elem)) {
        gaddrof(); vtop->type = char_pointer_type;
        vpushi(lane * 4); gen_op('+');
        vtop->type = elem; vtop->r |= VT_LVAL;
    }
}

static int bcl_vec_cast(CType* type) {
    CType dst, src;
    int dn = bcl_vec_info(type, &dst), sn = bcl_vec_info(&vtop->type, &src);
    SValue value, result;
    int i;
    if (!dn && !sn) return 0;
    if (!dn || (sn && sn != dn)) tcc_error("invalid OpenCL vector conversion");
    if (sn && is_compatible_types(type, &vtop->type)) { vtop->type = *type; return 1; }
    save_regs(0);
    value = *vtop; vtop--;
    value = bcl_vec_save(value);
    result = bcl_vec_temp(*type);
    for (i = 0; i < dn; i++) {
        bcl_vec_lane(&result, i); bcl_vec_lane(&value, i);
        gen_cast(&dst); vstore(); vpop();
    }
    vpushv(&result);
    return 1;
}

static int bcl_vec_binary(int op) {
    CType left_elem, right_elem, type;
    int ln = bcl_vec_info(&vtop[-1].type, &left_elem), rn = bcl_vec_info(&vtop->type, &right_elem);
    int n = ln ? ln : rn, i, compare = TOK_ISCOND(op);
    SValue left, right, result;
    if (!n) return 0;
    if (ln && rn && (ln != rn || left_elem.t != right_elem.t)) tcc_error("OpenCL vectors must have matching types");
    if (!(op == '+' || op == '-' || op == '*' || op == '/' || op == '%' || op == '&' || op == '|' || op == '^' ||
          op == TOK_SHL || op == TOK_SHR || op == TOK_SAR || compare)) tcc_error("unsupported OpenCL vector operation");
    type = ln ? vtop[-1].type : vtop->type;
    if (compare) type = bcl_vec_type(n, VT_INT);
    save_regs(0);
    right = *vtop--; left = *vtop--;
    left = bcl_vec_save(left); right = bcl_vec_save(right);
    result = bcl_vec_temp(type);
    for (i = 0; i < n; i++) {
        bcl_vec_lane(&result, i);
        bcl_vec_lane(&left, i);
        gen_cast(ln ? &left_elem : &right_elem);
        bcl_vec_lane(&right, i);
        gen_cast(ln ? &left_elem : &right_elem);
        gen_op(op);
        if (compare) { gen_cast_s(VT_INT); vpushi(-1); gen_op('*'); }
        vstore(); vpop();
    }
    vpushv(&result);
    return 1;
}

/* Floating negation must flip the sign of zero as well as ordinary lanes. */
static int bcl_vec_neg(void) {
    CType elem;
    int n = bcl_vec_info(&vtop->type, &elem), i;
    SValue value, result;
    if (!n || (elem.t & VT_BTYPE) != VT_FLOAT) return 0;
    save_regs(0);
    value = *vtop--; value = bcl_vec_save(value);
    result = bcl_vec_temp(value.type);
    for (i = 0; i < n; i++) {
        bcl_vec_lane(&result, i); bcl_vec_lane(&value, i);
        gen_opif(TOK_NEG); vstore(); vpop();
    }
    vpushv(&result);
    return 1;
}

/* OpenCL constructors use (float4)(a,b,c,d), not C's comma operator. */
static void bcl_vec_constructor(CType* type) {
    CType elem;
    int n = bcl_vec_info(type, &elem), used = 0;
    SValue result = bcl_vec_temp(*type), first;
    skip('(');
    for (;;) {
        CType src;
        int width, k;
        SValue value;
        expr_eq(); save_regs(0);
        width = bcl_vec_info(&vtop->type, &src);
        value = *vtop--; value = bcl_vec_save(value);
        if (!used) first = value;
        if (!width) width = 1;
        if (used + width > n) tcc_error("too many OpenCL vector constructor components");
        for (k = 0; k < width; k++) {
            bcl_vec_lane(&result, used++); bcl_vec_lane(&value,k);
            gen_cast(&elem); vstore(); vpop();
        }
        if (tok != ',') break;
        next();
    }
    skip(')');
    if (used == 1) {
        for (; used < n; used++) {
            bcl_vec_lane(&result,used); bcl_vec_lane(&first,0);
            gen_cast(&elem); vstore(); vpop();
        }
    } else if (used != n) tcc_error("not enough OpenCL vector constructor components");
    vpushv(&result);
}

/* Multi-component swizzles are values; single components remain C lvalues. */
static int bcl_vec_swizzle(void) {
    CType elem, type;
    int width = bcl_vec_info(&vtop->type, &elem), lanes[16], n = 0, i;
    const char* name;
    SValue value, result;
    if (!width) return 0;
    name = get_tok_str(tok, NULL);
    if (!name) return 0;
    if (!strcmp(name,"lo") || !strcmp(name,"hi") || !strcmp(name,"even") || !strcmp(name,"odd")) {
        int padded = width == 3 ? 4 : width;
        for (i=0;i<padded/2;i++) lanes[n++] = (!strcmp(name,"lo")) ? i : (!strcmp(name,"hi")) ? i+padded/2 : i*2+(!strcmp(name,"odd"));
    } else {
        int numeric = name[0] == 's' || name[0] == 'S';
        if (numeric) name++;
        for (; *name; name++) {
            const char* p = strchr(numeric ? "0123456789abcdef" : "xyzw", *name);
            int lane = p ? (int)(p - (numeric ? "0123456789abcdef" : "xyzw")) : -1;
            if (lane < 0 || lane >= width || n == 16) return 0;
            lanes[n++] = lane;
        }
    }
    if (n <= 1) return 0;
    if (n != 2 && n != 3 && n != 4 && n != 8 && n != 16) tcc_error("invalid OpenCL vector swizzle");
    type = bcl_vec_type(n, elem.t);
    save_regs(0); value = *vtop--; value = bcl_vec_save(value);
    result = bcl_vec_temp(type);
    for (i = 0; i < n; i++) {
        bcl_vec_lane(&result,i); bcl_vec_lane(&value,lanes[i]); vstore(); vpop();
    }
    vpushv(&result); next();
    if (tok == '=' || tok == TOK_INC || tok == TOK_DEC || TOK_ASSIGN(tok))
        tcc_error("multi-lane OpenCL swizzle stores are not supported");
    return 1;
}
