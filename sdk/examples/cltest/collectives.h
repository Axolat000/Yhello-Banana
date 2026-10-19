/* SPDX-License-Identifier: BSD-2-Clause */
/* Behavioral tests of compiled kernels, shared memory and saved work-items. */
static void collective_tests(cl_context ctx, cl_device_id dev, cl_command_queue q) {
    static const char* src =
        "void sync_group(void) { barrier(CLK_LOCAL_MEM_FENCE | CLK_GLOBAL_MEM_FENCE); }\n"
        "kernel void reduce(global int* out, local int* tile) {\n"
        " int l=get_local_id(0), g=get_global_id(0), keep=g*7+3;\n"
        " for(int round=0;round<4;round++) {\n"
        " tile[l]=g+round; sync_group();\n"
        " for(int stride=8;stride;stride/=2) { if(l<stride) tile[l]+=tile[l+stride]; barrier(CLK_LOCAL_MEM_FENCE); }\n"
        " if(!l) out[get_group_id(0)*4+round]=tile[0]; barrier(CLK_LOCAL_MEM_FENCE); }\n"
        " out[32+g]=keep; }\n"
        "kernel void tile2d(global int* out) {\n"
        " local int tile[4][4]; int x=get_local_id(0),y=get_local_id(1);\n"
        " tile[y][x]=get_global_id(0)+100*get_global_id(1); barrier(CLK_LOCAL_MEM_FENCE);\n"
        " out[(get_global_id(1)-3)*8+get_global_id(0)-5]=tile[3-y][3-x]; }\n"
        "kernel void diverge(global int* out) { if(get_local_id(0)==0) return; barrier(CLK_LOCAL_MEM_FENCE); out[0]=1; }\n"
        "kernel void sites(global int* out) { if(get_local_id(0)&1) barrier(1); else barrier(1); out[0]=1; }\n"
        "kernel void oversized(global int* out) { local int large[9000]; large[get_local_id(0)]=1; barrier(1); out[0]=large[0]; }\n"
        "kernel void vectors(global float* out, float4 bias) {\n"
        " int i=get_global_id(0); float4 a=(float4)(1,2,3,4);\n"
        " float2 b=(float2)(5,6); float4 c=(float4)(b,7,8);\n"
        " float4 r=(a*2+c-bias)/2; r=-(-r); r+=1; r-=1; r.x+=i; r.y=-r.y;\n"
        " vstore4(r.wzyx, i, out); }\n"
        "kernel void packed(global float* out, global float* in) {\n"
        " int i=get_global_id(0); float3 a=vload3(i,in);\n"
        " float3 b=sqrt(a*a); b=b+1; vstore3(b,i,out);\n"
        " if(!i) out[24]=sizeof(float3); }\n"
        "kernel void masks(global int* out) {\n"
        " float4 a=(float4)(1,2,3,4); int4 m=a>(float4)(2);\n"
        " uint4 u=(uint4)(1,2,3,4); u=(u<<1)^(uint4)(3);\n"
        " vstore4(m,0,out); vstore4((int4)u,1,out);\n"
        " float4 b=clamp(a*2,3.0f,6.0f); vstore4((int4)b,2,out);\n"
        " out[12]=(int)dot(a,a); out[13]=(int)length((float2)(3,4));\n"
        " float4 c=normalize((float4)(0,0,0,2)); out[14]=(int)c.w;\n"
        " float8 d=(float8)(a,a+4); float16 e=(float16)(d,d);\n"
        " out[15]=(int)e.sfc.x; out[16]=(int)e.even.s7; float4 zero=-(float4)(0.0f,-0.0f,1,-1); out[17]=1.0f/zero.x<0; out[18]=1.0f/zero.y>0; }\n"
        "kernel void exchange(global int* p) {\n"
        " int x=get_global_id(0),y=get_global_id(1),z=get_global_id(2),i=x+4*y+16*z;\n"
        " float keep=i*0.5f+0.25f; p[i]=i*3; mem_fence(CLK_GLOBAL_MEM_FENCE); barrier(CLK_GLOBAL_MEM_FENCE);\n"
        " int other=(x^1)+4*(y^1)+16*(z^1); p[64+i]=p[other]+(int)(keep*4)+get_work_dim()-3; }\n"
        "kernel void sharedvec(global float* out) {\n"
        " local float4 tile[16]; int l=get_local_id(0);\n"
        " float4 keep=(float4)(get_global_id(0),2,3,4); tile[l]=keep; barrier(1);\n"
        " float4 r=tile[15-l]+keep; barrier(1); vstore4(r,get_global_id(0),out); }\n";
    cl_int err;
    cl_program p=clCreateProgramWithSource(ctx,1,&src,NULL,&err);
    err=clBuildProgram(p,1,&dev,NULL,NULL,NULL);
    if(err) { char log[4096]; clGetProgramBuildInfo(p,dev,CL_PROGRAM_BUILD_LOG,sizeof(log),log,NULL); printf("collective build log:\n%s\n",log); }
    CHECK(err==CL_SUCCESS,"build barriers, local arrays and vector kernels");
    if(err) { clReleaseProgram(p); return; }
    cl_mem mem=clCreateBuffer(ctx,CL_MEM_READ_WRITE,4096,NULL,&err);
    cl_kernel k=clCreateKernel(p,"reduce",&err);
    clSetKernelArg(k,0,sizeof(mem),&mem); clSetKernelArg(k,1,16*sizeof(int),NULL);
    size_t gs=128,ls=16; int data[256];
    err=clEnqueueNDRangeKernel(q,k,1,NULL,&gs,&ls,0,NULL,NULL);
    clEnqueueReadBuffer(q,mem,CL_TRUE,0,sizeof(data),data,0,NULL,NULL);
    int good=!err;
    for(int group=0;group<8;group++) for(int r=0;r<4;r++) if(data[group*4+r]!=256*group+120+16*r) good=0;
    for(int i=0;i<128;i++) if(data[32+i]!=i*7+3) good=0;
    CHECK(good,"repeated reductions, helper barriers, private variables survive");
    clSetKernelArg(k,1,32769,NULL);
    CHECK(clEnqueueNDRangeKernel(q,k,1,NULL,&gs,&ls,0,NULL,NULL)==CL_OUT_OF_RESOURCES,"local arguments enforce the 32 KiB limit");
    clReleaseKernel(k);
    k=clCreateKernel(p,"tile2d",&err); clSetKernelArg(k,0,sizeof(mem),&mem);
    size_t g2[2]={8,8},l2[2]={4,4},off[2]={5,3};
    err=clEnqueueNDRangeKernel(q,k,2,off,g2,l2,0,NULL,NULL);
    clEnqueueReadBuffer(q,mem,CL_TRUE,0,64*sizeof(int),data,0,NULL,NULL); good=!err;
    for(int y=0;y<8;y++) for(int x=0;x<8;x++) if(data[y*8+x]!=(x/4)*4+3-x%4+5+100*((y/4)*4+3-y%4+3)) good=0;
    CHECK(good,"shared 2D local arrays, 2D groups and global offsets"); clReleaseKernel(k);
    const char* invalid[]={"diverge","sites","oversized"};
    for(int i=0;i<3;i++) {
        k=clCreateKernel(p,invalid[i],&err); clSetKernelArg(k,0,sizeof(mem),&mem);
        err=clEnqueueNDRangeKernel(q,k,1,NULL,&gs,&ls,0,NULL,NULL);
        CHECK(err==(i==2?CL_OUT_OF_RESOURCES:CL_INVALID_OPERATION),invalid[i]); clReleaseKernel(k);
    }
    k=clCreateKernel(p,"vectors",&err); clSetKernelArg(k,0,sizeof(mem),&mem);
    cl_float4 bias={{1,2,3,4}}; clSetKernelArg(k,1,sizeof(bias),&bias);
    gs=8;ls=4; float f[256];
    err=clEnqueueNDRangeKernel(q,k,1,NULL,&gs,&ls,0,NULL,NULL);
    clEnqueueReadBuffer(q,mem,CL_TRUE,0,32*sizeof(float),f,0,NULL,NULL); good=!err;
    for(int i=0;i<8;i++) if(f[4*i]!=6||f[4*i+1]!=5||f[4*i+2]!=-4||f[4*i+3]!=3+i) { printf("vector[%d]: %f %f %f %f\n",i,f[4*i],f[4*i+1],f[4*i+2],f[4*i+3]);good=0; }
    CHECK(good,"float4 constructors, arithmetic, swizzles and by-value host arguments"); clReleaseKernel(k);
    float input[24];for(int i=0;i<24;i++) input[i]=i-12;
    cl_mem in=clCreateBuffer(ctx,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,sizeof(input),input,&err);
    k=clCreateKernel(p,"packed",&err);clSetKernelArg(k,0,sizeof(mem),&mem);clSetKernelArg(k,1,sizeof(in),&in);
    err=clEnqueueNDRangeKernel(q,k,1,NULL,&gs,&ls,0,NULL,NULL);
    clEnqueueReadBuffer(q,mem,CL_TRUE,0,25*sizeof(float),f,0,NULL,NULL);good=!err&&f[24]==16;
    for(int i=0;i<24;i++) if(f[i]!=fabsf(input[i])+1) good=0;
    CHECK(good,"float3 padded layout, packed vload3/vstore3 and vector sqrt"); clReleaseKernel(k);clReleaseMemObject(in);
    k=clCreateKernel(p,"masks",&err);clSetKernelArg(k,0,sizeof(mem),&mem);size_t one=1;
    err=clEnqueueNDRangeKernel(q,k,1,NULL,&one,&one,0,NULL,NULL);
    clEnqueueReadBuffer(q,mem,CL_TRUE,0,19*sizeof(int),data,0,NULL,NULL);
    int expected[]={0,0,-1,-1,1,7,5,11,3,4,6,6,30,5,1,8,7,1,1};
    good=!err;for(int i=0;i<19;i++) if(data[i]!=expected[i]) {printf("mask[%d]=%d want %d\n",i,data[i],expected[i]);good=0;}
    CHECK(good,"int/uint masks, shifts, clamp, dot, normalize, float8/16"); clReleaseKernel(k);
    k=clCreateKernel(p,"sharedvec",&err);clSetKernelArg(k,0,sizeof(mem),&mem);gs=64;ls=16;
    err=clEnqueueNDRangeKernel(q,k,1,NULL,&gs,&ls,0,NULL,NULL);
    clEnqueueReadBuffer(q,mem,CL_TRUE,0,sizeof(f),f,0,NULL,NULL);good=!err;
    for(int i=0;i<64;i++) if(f[i*4]!=(i/16)*32+15||f[i*4+1]!=4||f[i*4+2]!=6||f[i*4+3]!=8) good=0;
    CHECK(good,"local float4 arrays and vector private state across barriers");clReleaseKernel(k);
    k=clCreateKernel(p,"exchange",&err);clSetKernelArg(k,0,sizeof(mem),&mem);
    size_t g3[3]={4,4,4},l3[3]={2,2,2};
    err=clEnqueueNDRangeKernel(q,k,3,NULL,g3,l3,0,NULL,NULL);
    clEnqueueReadBuffer(q,mem,CL_TRUE,0,128*sizeof(int),data,0,NULL,NULL);good=!err;
    for(int z=0;z<4;z++) for(int y=0;y<4;y++) for(int x=0;x<4;x++) {
        int i=x+4*y+16*z, other=(x^1)+4*(y^1)+16*(z^1);
        if(data[64+i]!=other*3+2*i+1) good=0;
    }
    CHECK(good,"global-memory fences, 3D groups and private float state");clReleaseKernel(k);
    clReleaseMemObject(mem);clReleaseProgram(p);
    const char* badswizzle="kernel void bad(global float* p) { float4 a=(float4)(0); a.xy += (float2)(1); p[0]=a.x; }";
    p=clCreateProgramWithSource(ctx,1,&badswizzle,NULL,&err);
    CHECK(clBuildProgram(p,1,&dev,NULL,NULL,NULL)==CL_BUILD_PROGRAM_FAILURE,"unsupported multi-component swizzle stores report a build error");
    clReleaseProgram(p);
}
