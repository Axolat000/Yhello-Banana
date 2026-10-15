#!/usr/bin/env python3
"""Apply the OpenCL-only TinyCC extension to a generated compiler copy."""
from pathlib import Path
import sys, shutil
root = Path(__file__).resolve().parent
out = Path(sys.argv[1])
p = out / 'tccgen.c'
s = p.read_text()
if '#include "clvectors-tcc.h"' not in s:
    def replace(old, new):
        global s
        if s.count(old) != 1:
            raise SystemExit('OpenCL compiler patch: unexpected TinyCC source at ' + old[:70])
        s = s.replace(old, new)
    replace('ST_FUNC void gen_op(int op)\n{', '#include "clvectors-tcc.h"\n\nST_FUNC void gen_op(int op)\n{')
    replace('    int op_class = op;\n', '    int op_class = op;\n\n    if (bcl_vec_binary(op)) return;\n')
    replace('    int dbt_bt, sbt_bt, ds, ss, bits, trunc;\n', '    int dbt_bt, sbt_bt, ds, ss, bits, trunc;\n\n    if (bcl_vec_cast(type)) return;\n')
    replace('    st = &vtop->type; /* source type */', '    if (bcl_vec_info(dt, &(CType){0})) { bcl_vec_cast(dt); return; }\n    st = &vtop->type; /* source type */')
    replace("            } else if (t == TOK_SOTYPE) { /* from sizeof/alignof (...) */", "            } else if (t != TOK_SOTYPE && tok == '(' && bcl_vec_info(&type, &(CType){0})) {\n                bcl_vec_constructor(&type);\n            } else if (t == TOK_SOTYPE) { /* from sizeof/alignof (...) */")
    replace('if (is_float(vtop->type.t)) {\n            gen_opif(TOK_NEG);', 'if (bcl_vec_neg()) {\n        } else if (is_float(vtop->type.t)) {\n            gen_opif(TOK_NEG);')
    replace('s = find_field(&vtop->type, tok, &cumofs);', 'if (bcl_vec_swizzle()) continue;\n            s = find_field(&vtop->type, tok, &cumofs);')
    p.write_text(s)
shutil.copyfile(root/'clvectors-tcc.h', out/'clvectors-tcc.h')
