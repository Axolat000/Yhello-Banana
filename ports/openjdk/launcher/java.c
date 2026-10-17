/* java - the Java launcher for Banana OS (OpenJDK 17: the server VM, or Zero).
 *
 *   java [options] <main class> [args...]
 *   java [options] -jar <file.jar> [args...]
 *   java -version
 *
 * Loads the JVM (JAVA_HOME/lib/server/libjvm.so, a shared library opened
 * with the SDK's dlopen), creates it on a thread with a big stack and runs
 * the main class. JAVA_HOME: the environment variable, else the first of
 * /usr/java/jdk (the java package), /jdk, /mnt/jdk, /home/banana/jdk that has the JVM. Options: -cp /
 * -classpath / --class-path, -D..., -X... (to the JVM), -verbose:..., -ea. */
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <jni.h>

typedef jint (JNICALL* create_vm_t)(JavaVM**, void**, void*);

static const char* g_home;
static JavaVMOption g_opts[64];
static int g_nopts;
static const char* g_main;          /* the main class (dots or slashes) */
static int g_is_jar, g_version, g_argc;
static char** g_argv;
static int g_exit;

static int has_jvm(const char* home) {
    char p[512];
    snprintf(p, sizeof(p), "%s/lib/server/libjvm.so", home);
    return access(p, 0) == 0;
}

static void opt(const char* s) { if (g_nopts < 64) g_opts[g_nopts++].optionString = (char*)s; }

static void usage(void) {
    fprintf(stderr, "usage: java [options] <main class> [args...]\n"
                    "       java [options] -jar <file.jar> [args...]\n"
                    "       java -version\n"
                    "options: -cp <path>, -D<name>=<value>, -X<option>, -ea, -verbose:<what>\n");
}

/* the main class of a jar: its manifest's Main-Class (java.util.jar does it) */
static jstring jar_main(JNIEnv* env, const char* jar) {
    jclass jf = (*env)->FindClass(env, "java/util/jar/JarFile");
    if (!jf) return NULL;
    jmethodID init = (*env)->GetMethodID(env, jf, "<init>", "(Ljava/lang/String;)V");
    jmethodID man = (*env)->GetMethodID(env, jf, "getManifest", "()Ljava/util/jar/Manifest;");
    jobject f = (*env)->NewObject(env, jf, init, (*env)->NewStringUTF(env, jar));
    if (!f) return NULL;
    jobject m = (*env)->CallObjectMethod(env, f, man);
    if (!m) return NULL;
    jclass mc = (*env)->FindClass(env, "java/util/jar/Manifest");
    jobject attrs = (*env)->CallObjectMethod(env, m, (*env)->GetMethodID(env, mc, "getMainAttributes", "()Ljava/util/jar/Attributes;"));
    jclass ac = (*env)->FindClass(env, "java/util/jar/Attributes");
    return (jstring)(*env)->CallObjectMethod(env, attrs, (*env)->GetMethodID(env, ac, "getValue", "(Ljava/lang/String;)Ljava/lang/String;"),
                                            (*env)->NewStringUTF(env, "Main-Class"));
}

static void* run(void* arg) {
    (void)arg;
    char jvm[512];
    snprintf(jvm, sizeof(jvm), "%s/lib/server/libjvm.so", g_home);
    void* lib = dlopen(jvm, RTLD_NOW | RTLD_GLOBAL);
    if (!lib) { fprintf(stderr, "java: cannot load the JVM: %s\n", dlerror()); g_exit = 1; return NULL; }
    create_vm_t create = (create_vm_t)dlsym(lib, "JNI_CreateJavaVM");
    if (!create) { fprintf(stderr, "java: %s has no JNI_CreateJavaVM\n", jvm); g_exit = 1; return NULL; }

    JavaVMInitArgs args;
    args.version = JNI_VERSION_10;
    args.options = g_opts;
    args.nOptions = g_nopts;
    args.ignoreUnrecognized = JNI_FALSE;
    JavaVM* vm;
    JNIEnv* env;
    if (create(&vm, (void**)&env, &args) != JNI_OK) { fprintf(stderr, "java: the JVM could not start\n"); g_exit = 1; return NULL; }

    if (g_version) {
        jclass vp = (*env)->FindClass(env, "java/lang/VersionProps");
        jmethodID pr = vp ? (*env)->GetStaticMethodID(env, vp, "print", "(Z)V") : NULL;
        if (pr) (*env)->CallStaticVoidMethod(env, vp, pr, JNI_TRUE);
    } else {
        char name[512];
        if (g_is_jar) {
            jstring mc = jar_main(env, g_main);
            if (!mc) { fprintf(stderr, "java: no Main-Class in %s\n", g_main); g_exit = 1; goto done; }
            const char* s = (*env)->GetStringUTFChars(env, mc, NULL);
            snprintf(name, sizeof(name), "%s", s);
            (*env)->ReleaseStringUTFChars(env, mc, s);
        } else {
            snprintf(name, sizeof(name), "%s", g_main);
        }
        for (char* c = name; *c; c++) if (*c == '.') *c = '/';
        jclass cls = (*env)->FindClass(env, name);
        if (!cls) {
            (*env)->ExceptionClear(env);
            fprintf(stderr, "Error: Could not find or load main class %s\n", g_main);
            g_exit = 1;
            goto done;
        }
        jmethodID mainm = (*env)->GetStaticMethodID(env, cls, "main", "([Ljava/lang/String;)V");
        if (!mainm) { (*env)->ExceptionClear(env); fprintf(stderr, "Error: no main method in %s\n", name); g_exit = 1; goto done; }
        jobjectArray jargs = (*env)->NewObjectArray(env, g_argc, (*env)->FindClass(env, "java/lang/String"), NULL);
        for (int i = 0; i < g_argc; i++) (*env)->SetObjectArrayElement(env, jargs, i, (*env)->NewStringUTF(env, g_argv[i]));
        (*env)->CallStaticVoidMethod(env, cls, mainm, jargs);
    }
done:
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); g_exit = 1; }
    (*vm)->DestroyJavaVM(vm);
    return NULL;
}

int main(int argc, char** argv) {
    g_home = getenv("JAVA_HOME");
    static const char* const homes[] = { "/usr/java/jdk", "/jdk", "/mnt/jdk", "/home/banana/jdk", "/mnt/usb/jdk", "/mnt/virtio/jdk", "/mnt/sata/jdk", NULL };
    for (int i = 0; (!g_home || !has_jvm(g_home)) && homes[i]; i++) if (has_jvm(homes[i])) g_home = homes[i];
    if (!g_home || !has_jvm(g_home)) {
        fprintf(stderr, "java: no JDK found (JAVA_HOME, /usr/java/jdk, /jdk, /mnt/jdk or /home/banana/jdk with lib/server/libjvm.so)\n");
        return 1;
    }
    static char cp[1024] = "-Djava.class.path=.";
    int i = 1;
    for (; i < argc; i++) {
        const char* a = argv[i];
        if (!strcmp(a, "-version") || !strcmp(a, "--version")) { g_version = 1; continue; }
        if (!strcmp(a, "-help") || !strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        if ((!strcmp(a, "-cp") || !strcmp(a, "-classpath") || !strcmp(a, "--class-path")) && i + 1 < argc) {
            snprintf(cp, sizeof(cp), "-Djava.class.path=%s", argv[++i]);
            continue;
        }
        if (!strcmp(a, "-jar") && i + 1 < argc) {
            g_is_jar = 1;
            g_main = argv[++i];
            snprintf(cp, sizeof(cp), "-Djava.class.path=%s", g_main);
            i++;
            break;
        }
        if (!strcmp(a, "-ea") || !strcmp(a, "-enableassertions")) { opt("-ea"); continue; }
        if (a[0] == '-' && (a[1] == 'D' || a[1] == 'X' || !strncmp(a, "-verbose", 8))) { opt(a); continue; }
        if (a[0] == '-') { fprintf(stderr, "java: unknown option %s\n", a); usage(); return 1; }
        g_main = a;
        i++;
        break;
    }
    if (!g_main && !g_version) { usage(); return 1; }
    opt(cp);
    opt("-Dsun.java.launcher=SUN_STANDARD");
    g_argc = argc - i;
    g_argv = argv + i;

    /* the JVM on a thread with a big stack (the main thread has 1 MiB) */
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 8u << 20);
    pthread_t t;
    if (pthread_create(&t, &at, run, NULL) != 0) { fprintf(stderr, "java: cannot start the JVM's thread\n"); return 1; }
    pthread_join(t, NULL);
    return g_exit;
}
