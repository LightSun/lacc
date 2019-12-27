#if AMALGAMATION
# define INTERNAL static
# define EXTERNAL static
# include "context.c"
# include "util/argparse.c"
# include "util/hash.c"
# include "util/string.c"
# include "backend/x86_64/encoding.c"
# include "backend/x86_64/dwarf.c"
# include "backend/x86_64/elf.c"
# include "backend/x86_64/abi.c"
# include "backend/x86_64/assemble.c"
# include "backend/assembler.c"
# include "backend/compile.c"
# include "backend/graphviz/dot.c"
# include "backend/linker.c"
# include "optimizer/transform.c"
# include "optimizer/liveness.c"
# include "optimizer/optimize.c"
# include "preprocessor/tokenize.c"
# include "preprocessor/strtab.c"
# include "preprocessor/input.c"
# include "preprocessor/directive.c"
# include "preprocessor/preprocess.c"
# include "preprocessor/macro.c"
# include "parser/typetree.c"
# include "parser/symtab.c"
# include "parser/parse.c"
# include "parser/statement.c"
# include "parser/initializer.c"
# include "parser/expression.c"
# include "parser/declaration.c"
# include "parser/eval.c"
#else
# define INTERNAL
# define EXTERNAL extern
# include "backend/compile.h"
# include "backend/linker.h"
# include "optimizer/optimize.h"
# include "parser/parse.h"
# include "parser/symtab.h"
# include "parser/typetree.h"
# include "preprocessor/preprocess.h"
# include "preprocessor/input.h"
# include "preprocessor/macro.h"
# include "util/argparse.h"
# include <lacc/context.h>
# include <lacc/ir.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>

/*
 * Configurable location of where compiler is installed. Headers can be
 * found under include folder.
 */
#ifndef LACC_LIB_PATH
# define LACC_LIB_PATH "/usr/local/lib/lacc"
#endif

/*
 * Configurable location of system headers. Default on Linux is GNU
 * libc. Can be overridden to point to for example musl.
 *
 * OpenBSD does not need a special path.
 */
#ifndef SYSTEM_LIB_PATH
# ifdef __linux__
#  define SYSTEM_LIB_PATH "/usr/include/x86_64-linux-gnu"
# endif
#endif

static enum lang {
    LANG_UNKNOWN,
    LANG_C,
    LANG_ASM
} source_language;

struct input_file {
    const char *name;
    const char *output_name;
    const char *makefile_name;
    const char *makefile_target;
    int is_default_name;
    enum lang language;
};

static const char *program, *output_name;
static const char *makefile_name, *makefile_target;
static int optimization_level;
static int dump_symbols, dump_types;
static int nostdinc;

static array_of(struct input_file) input_files;
static array_of(char *) predefined_macros;
static array_of(const char *) system_include_paths;

static int help(const char *arg)
{
    fprintf(
        stderr,
        "Usage: %s [-(S|E|c)] [-I <path>] [-o <file>] <file ...>\n",
        program);
    return 1;
}

static int flag(const char *arg)
{
    assert(arg[0] == '-');
    assert(strlen(arg) == 2);
    switch (arg[1]) {
    case 'c':
        context.target = TARGET_x86_64_OBJ;
        break;
    case 'S':
        context.target = TARGET_x86_64_ASM;
        break;
    case 'E':
        context.target = TARGET_PREPROCESS;
        break;
    case 'v':
        context.verbose += 1;
        break;
    case 'g':
        context.debug = 1;
        break;
    case 'w':
        context.suppress_warning = 1;
        break;
    default:
        assert(0);
        break;
    }

    return 0;
}

static int option(const char *arg)
{
    int disable;

    assert(*arg == '-');
    if (arg[1] == 'f') {
        arg = arg + 2;
        disable = strncmp("no-", arg, 3) == 0;
        if (disable) {
            arg = arg + 3;
        }
        if (!strcmp("PIC", arg)) {
            context.pic = !disable;
        } else if (!strcmp("common", arg)) {
            context.no_common = disable;
        } else if (!strcmp("fast-math", arg)) {
            /* Always slow... */
        } else if (!strcmp("strict-aliasing", arg)) {
            /* We don't consider aliasing. */
        } else if (!strcmp("short-wchar", arg)) {
            /* What to do here? */
        } else assert(0);
    } else if (arg[1] == 'm') {
        arg = arg + 2;
        disable = strncmp("no-", arg, 3) == 0;
        if (disable) {
            arg = arg + 3;
        }
        if (!strcmp("sse", arg) || !strcmp("sse2", arg)
            || !strcmp("mmx", arg)
            || !strcmp("3dnow", arg))
        {
            context.no_sse = 1;
        } else if (!strcmp("red-zone", arg)) {
            /* Don't use red-zone in any case. */
        } else assert(0);
    } else if (!strcmp("-dot", arg)) {
        context.target = TARGET_IR_DOT;
    } else if (!strcmp("-nostdinc", arg)) {
        nostdinc = 1;
    }

    return 0;
}

/*
 -M, -MD, -MF, -MG, -MM, -MMD -MP, -MQ, -MT

   -M  Instead of outputting the result of preprocessing, output a rule
       suitable for make describing the dependencies of the main source
       file.  The preprocessor outputs one make rule containing the object
       file name for that source file, a colon, and the names of all the
       included files, including those coming from -include or -imacros
       command-line options.

       Unless specified explicitly (with -MT or -MQ), the object file name
       consists of the name of the source file with any suffix replaced
       with object file suffix and with any leading directory parts
       removed.  If there are many included files then the rule is split
       into several lines using \-newline.  The rule has no commands.

       This option does not suppress the preprocessor's debug output, such
       as -dM.  To avoid mixing such debug output with the dependency
       rules you should explicitly specify the dependency output file with
       -MF, or use an environment variable like DEPENDENCIES_OUTPUT.
       Debug output is still sent to the regular output stream as normal.

       Passing -M to the driver implies -E, and suppresses warnings with
       an implicit -w.

   -MM Like -M but do not mention header files that are found in system
       header directories, nor header files that are included, directly or
       indirectly, from such a header.

       This implies that the choice of angle brackets or double quotes in
       an #include directive does not in itself determine whether that
       header appears in -MM dependency output.

   -MF file
       When used with -M or -MM, specifies a file to write the
       dependencies to.  If no -MF switch is given the preprocessor sends
       the rules to the same place it would send preprocessed output.

       When used with the driver options -MD or -MMD, -MF overrides the
       default dependency output file.

   -MG In conjunction with an option such as -M requesting dependency
       generation, -MG assumes missing header files are generated files
       and adds them to the dependency list without raising an error.  The
       dependency filename is taken directly from the "#include" directive
       without prepending any path.  -MG also suppresses preprocessed
       output, as a missing header file renders this useless.

       This feature is used in automatic updating of makefiles.
   -MP This option instructs CPP to add a phony target for each dependency
       other than the main file, causing each to depend on nothing.  These
       dummy rules work around errors make gives if you remove header
       files without updating the Makefile to match.

       This is typical output:

               test.o: test.c test.h

               test.h:

   -MT target
       Change the target of the rule emitted by dependency generation.  By
       default CPP takes the name of the main input file, deletes any
       directory components and any file suffix such as .c, and appends
       the platform's usual object suffix.  The result is the target.

       An -MT option sets the target to be exactly the string you specify.
       If you want multiple targets, you can specify them as a single
       argument to -MT, or use multiple -MT options.

       For example, -MT '$(objpfx)foo.o' might give

               $(objpfx)foo.o: foo.c

   -MQ target
       Same as -MT, but it quotes any characters which are special to
       Make.  -MQ '$(objpfx)foo.o' gives

               $$(objpfx)foo.o: foo.c

       The default target is automatically quoted, as if it were given
       with -MQ.

   -MMD
       Like -MD except mention only user header files, not system header
       files.

*/

INTERNAL struct dependency_config dependency_config;

static int dependency_option(const char *arg)
{
    struct dependency_config *opts = &dependency_config;
    assert(arg[0] == '-');
    assert(arg[1] == 'M');

    /* -M, -MD, -MF, -MG, -MM, -MMD -MP, -MQ, -MT */
    context.generate_dependencies = 1;
    if (!strcmp("-M", arg)) {
        flag("-E");
        flag("-w");
    } else if (!strcmp("-MM", arg)) {
        flag("-E");
        flag("-w");
        opts->skip_system_headers = 1;
    } else if (!strcmp("-MD", arg)) {
        flag("-w");
        opts->generate_file_name = 1;
    } else if (!strcmp("-MMD", arg)) {
        flag("-w");
        opts->generate_file_name = 1;
        opts->skip_system_headers = 1;
    } else if (!strcmp("-MG", arg)) {
        opts->accept_missing_headers = 1;
    } else if (!strcmp("-MP", arg)) {
        opts->phony_targets = 1;
    } else assert(0);

    return 0;
}

/*
 * Specify file name to write dependencies to.
 */
static int set_makefile_name(const char *name)
{
    makefile_name = name;
    return 0;
}

/*
 * -MT target
 *
 * Set name of target in makefile.
 */
static int set_makefile_target(const char *name)
{
    makefile_target = name;
    return 0;
}

/*
 * -MQ target
 *
 * Set target in makefile to a string with special chanacters in Make
 * syntax is quoted.
 */
static int set_makefile_target_quoted(const char *name)
{
    /* todo: Actual quoting */
    return set_makefile_target(name);
}

static int language(const char *arg)
{
    enum lang lang;

    assert(arg);
    if (!strcmp("c", arg)
        || !strcmp("c-header", arg)
        || !strcmp("c-cpp-output", arg))
    {
        lang = LANG_C;
    } else if (!strcmp("assembler", arg)) {
        lang = LANG_ASM;
    } else if (!strcmp("none", arg)) {
        lang = LANG_UNKNOWN;
    } else {
        fprintf(stderr, "Unrecognized input language %s.\n", arg);
        return 1;
    }

    source_language = lang;
    return 0;
}

static int cmodel(const char *arg)
{
    if (!strcmp("small", arg)) {
    } else if (!strcmp("kernel", arg)) {
    } else {
        fprintf(stderr, "Unrecognized memory model %s\n", arg);
        return 1;
    }

    return 0;
}

/* Support -fvisibility, with no effect. */
static int set_visibility(const char *arg)
{
    return 0;
}

/* Accept anything for -march. */
static int set_cpu(const char *arg)
{
    return 0;
}

/* Ignore all warning options specified with -W<option> */
static int warn(const char *arg)
{
    return 0;
}

static int ignore(const char *arg)
{
    return 0;
}

static int set_output_name(const char *file)
{
    output_name = file;
    add_linker_arg("-o");
    add_linker_arg(file);
    return 0;
}

static int add_system_include_path(const char *path)
{
    array_push_back(&system_include_paths, path);
    return 0;
}

/*
 * Write to default file if -o, -S or -dot is specified, using input
 * file name with suffix changed to '.o', '.s' or '.dot', respectively.
 *
 * We also need to strip any path information, so that lacc can write
 * its output to the current working directory. This matches what gcc
 * and clang do.
 */
static char *change_file_suffix(const char *file, enum target target)
{
    char *name, *suffix;
    const char *slash, *dot;
    size_t len;

    switch (target) {
    default: assert(0);
    case TARGET_PREPROCESS:
        return NULL;
    case TARGET_IR_DOT:
        suffix = ".dot";
        break;
    case TARGET_x86_64_ASM:
        suffix = ".s";
        break;
    case TARGET_x86_64_OBJ:
    case TARGET_x86_64_EXE:
        suffix = ".o";
        break;
    }

    slash = strrchr(file, '/');
    if (slash) {
        file = slash + 1;
    }

    dot = strrchr(file, '.');
    if (!dot) {
        dot = file + strlen(file);
    }

    len = (dot - file);
    name = calloc(len + strlen(suffix) + 1, sizeof(*name));
    strncpy(name, file, len);
    assert(!name[len] || name[len] == '.');
    name[len] = '.';
    strcpy(name + len, suffix);
    return name;
}

static const char *set_suffix(const char *path, const char *suffix)
{
    char *name;
    const char *slash, *dot;
    size_t len;

    assert(path);
    assert(suffix);

    slash = strrchr(path, '/');
    if (slash) {
        path = slash + 1;
    }

    dot = strrchr(path, '.');
    if (!dot) {
        dot = path + strlen(path);
    }

    len = (dot - path);
    name = calloc(len + strlen(suffix) + 1, 1);
    strncpy(name, path, len);
    name[len] = '.';
    strcpy(name + len, suffix);
    return name;
}

static int add_input_file(const char *name)
{
    char *ptr;
    struct input_file file = {0};

    if ((file.language = source_language) == LANG_UNKNOWN) {
        ptr = strrchr(name, '.');
        if (ptr && (ptr[1] == 'c' || ptr[1] == 'i') && ptr[2] == '\0') {
            file.language = LANG_C;
        }
    }

    file.name = name;
    array_push_back(&input_files, file);

    /*
     * Linker argument might not be needed, but make sure order is
     * preserved.
     */
    if (file.language != LANG_UNKNOWN) {
        ptr = change_file_suffix(name, TARGET_x86_64_OBJ);
        add_linker_arg(ptr);
        free(ptr);
    } else {
        add_linker_arg(name);
    }

    return 0;
}

static void clear_input_files(void)
{
    int i;
    struct input_file *file;

    for (i = 0; i < array_len(&input_files); ++i) {
        file = &array_get(&input_files, i);
        if (file->is_default_name) {
            free((void *) file->output_name);
        }
    }

    array_clear(&input_files);
}

static int set_c_std(const char *std)
{
    if (!strcmp("c89", std) || !strcmp("gnu89", std)) {
        context.standard = STD_C89;
    } else if (!strcmp("c99", std) || !strcmp("gnu99", std)) {
        context.standard = STD_C99;
    } else if (!strcmp("c11", std) || !strcmp("gnu11", std)) {
        context.standard = STD_C11;
    } else {
        fprintf(stderr, "Unrecognized c standard %s.\n", std);
        return 1;
    }

    return 0;
}

static int set_optimization_level(const char *level)
{
    assert(isdigit(level[2]));
    optimization_level = level[2] - '0';
    return 0;
}

static int long_option(const char *arg)
{
    if (!strcmp("--dump-symbols", arg)) {
        dump_symbols = 1;
    } else if (!strcmp("--dump-types", arg)) {
        dump_types = 1;
    }

    return 0;
}

static int define_macro(const char *arg)
{
    char *buf, *ptr;
    size_t len;

    len = strlen(arg) + 11;
    buf = calloc(len, sizeof(*buf));
    ptr = strchr(arg, '=');
    if (ptr) {
        sprintf(buf, "#define %s", arg);
        *(buf + 8 + (ptr - arg)) = ' ';
    } else {
        sprintf(buf, "#define %s 1", arg);
    }

    array_push_back(&predefined_macros, buf);
    return 0;
}

static void clear_predefined_macros(void)
{
    int i;
    char *buf;

    for (i = 0; i < array_len(&predefined_macros); ++i) {
        buf = array_get(&predefined_macros, i);
        free(buf);
    }

    array_clear(&predefined_macros);
}

static int add_linker_flag(const char *arg)
{
    char *end;

    if (!strcmp("-rdynamic", arg)) {
        add_linker_arg("-export-dynamic");
    } else {
        assert(!strncmp("-Wl,", arg, 4));
        end = strchr(arg, ',');
        do {
            arg = end + 1;
            end = strchr(arg, ',');
            if (end) {
                *end = '\0';
            }

            add_linker_arg(arg);
        } while (end);
    }

    return 0;
}

static int add_linker_library(const char *lib)
{
    if (lib[-2] == '-') {
        add_linker_arg(lib - 2);
    } else {
        add_linker_arg("-l");
        add_linker_arg(lib);
    }

    return 0;
}

static int add_linker_path(const char *path)
{
    if (path[-2] == '-') {
        add_linker_arg(path - 2);
    } else {
        add_linker_arg("-L");
        add_linker_arg(path);
    }

    return 0;
}

static int print_file_name(const char *name)
{
    FILE *f;
    char *path;

    assert(name);
    path = calloc(1, strlen(LACC_LIB_PATH) + strlen(name) + 2);
    strcpy(path, LACC_LIB_PATH);
    strcat(path, "/");
    strcat(path, name);
    if ((f = fopen(path, "r")) != 0) {
        printf("%s\n", path);
        fclose(f);
    } else {
        printf("%s\n", name);
    }

    free(path);
    return -1;
}

static int parse_program_arguments(int argc, char *argv[])
{
    int i, n, k;
    struct input_file *file;
    struct option optv[] = {
        {"-S", &flag},
        {"-E", &flag},
        {"-c", &flag},
        {"-v", &flag},
        {"-w", &flag},
        {"-g", &flag},
        {"-W", &warn},
        {"-W<", &warn},
        {"-x:", &language},
        {"-f[no-]PIC", &option},
        {"-f[no-]fast-math", &option},
        {"-f[no-]strict-aliasing", &option},
        {"-f[no-]common", &option},
        {"-f[no-]asynchronous-unwind-tables", &ignore},
        {"-f[no-]unused-but-set-variable", &ignore},
        {"-f[no-]omit-frame-pointer", &ignore},
        {"-f[no-]optimize-sibling-calls", &ignore},
        {"-fvisibility=", &set_visibility},
        {"-fshort-wchar", &option},
        {"-m[no-]sse", &option},
        {"-m[no-]sse2", &option},
        {"-m[no-]3dnow", &option},
        {"-m[no-]mmx", &option},
        {"-m[no-]red-zone", &option},
        {"-m64", &ignore},
        {"-mcmodel=", &cmodel},
        {"-dot", &option},
        {"--help", &help},
        {"-march=", &set_cpu},
        {"-o:", &set_output_name},
        {"-I:", &add_include_search_path},
        {"-O{0|1|2|3}", &set_optimization_level},
        {"-std=", &set_c_std},
        {"-D:", &define_macro},
        {"--dump-symbols", &long_option},
        {"--dump-types", &long_option},
        {"-nostdinc", &option},
        {"-isystem:", &add_system_include_path},
        {"-include:", &add_include_file},
        {"-print-file-name=", &print_file_name},
        {"-pipe", &option},
        {"-M", &dependency_option},
        {"-MF:", &set_makefile_name},
        {"-MT:", &set_makefile_target},
        {"-MQ:", &set_makefile_target_quoted},
        {"-M<", &dependency_option},
        {"-Wl,", &add_linker_flag},
        {"-rdynamic", &add_linker_flag},
        {"-shared", &add_linker_arg},
        {"-static", &add_linker_arg},
        {"-[no]pie", &add_linker_arg},
        {"-f[no-]PIE", &add_linker_arg},
        {"-l:", &add_linker_library},
        {"-L:", &add_linker_path},
        {NULL, &add_input_file}
    };

    program = argv[0];
    context.standard = STD_C99;
    context.target = TARGET_x86_64_EXE;
    context.pic = 1;

    if ((i = parse_args(optv, argc, argv)) != 0) {
        return i;
    }

    for (i = 0, k = 0; i < array_len(&input_files); ++i) {
        file = &array_get(&input_files, i);
        if (file->language == LANG_UNKNOWN) {
            switch (context.target) {
            case TARGET_PREPROCESS:
                file->language = LANG_C;
                break;
            case TARGET_x86_64_EXE:
                array_erase(&input_files, i);
                i--;
                k++;
                break;
            default:
                fprintf(stderr, "Unrecognized input file %s.\n", file->name);
                return 1;
            }
        }
    }

    n = array_len(&input_files);
    if (n == 0 && (k == 0 || context.target != TARGET_x86_64_EXE)) {
        fprintf(stderr, "%s\n", "No input files.");
        return 1;
    }

    if (output_name && context.target != TARGET_x86_64_EXE) {
        if (n > 1) {
            fprintf(stderr, "%s\n", "Cannot set -o with multiple inputs.");
            return 1;
        }

        assert(n == 1);
        file = &array_get(&input_files, 0);
        file->output_name = output_name;
    } else for (i = 0; i < n; ++i) {
        file = &array_get(&input_files, i);
        file->is_default_name = 1;
        file->makefile_target = makefile_target;
        switch (context.target) {
        case TARGET_PREPROCESS:
            if (context.generate_dependencies && !makefile_target) {
                file->makefile_target = set_suffix(file->name, ".o");
            }
            break;
        case TARGET_IR_DOT:
            file->output_name = set_suffix(file->name, ".dot");
            break;
        case TARGET_x86_64_ASM:
            file->output_name = set_suffix(file->name, ".s");
            break;
        case TARGET_x86_64_OBJ:
        case TARGET_x86_64_EXE:
            file->output_name = set_suffix(file->name, ".o");
            if (context.generate_dependencies && !makefile_target) {
                file->makefile_name = set_suffix(file->name, ".o.d");
                file->makefile_target = file->name;
            }
        default:
            break;
        }
    }

    return 0;
}

static void register_argument_definitions(void)
{
    int i;
    char *line;

    for (i = 0; i < array_len(&predefined_macros); ++i) {
        line = array_get(&predefined_macros, i);
        inject_line(line);
    }
}

/*
 * Register compiler internal builtin symbols, that are assumed to
 * exists by standard library headers.
 */
static void register_builtin_declarations(void)
{
    inject_line("void *memcpy(void *dest, const void *src, unsigned long n);");
    inject_line("void __builtin_alloca(unsigned long);");
    inject_line("void __builtin_va_start(void);");
    inject_line("void __builtin_va_arg(void);");
    inject_line(
        "typedef struct {"
        "   unsigned int gp_offset;"
        "   unsigned int fp_offset;"
        "   void *overflow_arg_area;"
        "   void *reg_save_area;"
        "} __builtin_va_list[1];");
}

/*
 * Add default search paths last, with lowest priority. These are
 * searched after anything specified with -I, and in the order listed.
 */
static void add_include_search_paths(void)
{
    int i;
    const char *path;

    if (!nostdinc) {
        add_include_search_path("/usr/local/include");
    }

    add_include_search_path(LACC_LIB_PATH "/include");
    if (!nostdinc) {
#ifdef SYSTEM_LIB_PATH
        add_include_search_path(SYSTEM_LIB_PATH);
#endif
        add_include_search_path("/usr/include");
    }

    for (i = 0; i < array_len(&system_include_paths); ++i) {
        path = array_get(&system_include_paths, i);
        add_include_search_path(path);
    }

    array_clear(&system_include_paths);
}

static int process_file(struct input_file file)
{
    FILE *output, *makefile;
    struct definition *def;
    const struct symbol *sym;

    preprocess_reset();
    set_input_file(file.name);
    register_builtin_definitions(context.standard);
    register_argument_definitions();
    if (file.output_name) {
        output = fopen(file.output_name, "w");
        if (!output) {
            fprintf(stderr, "Could not open output file '%s'.\n",
                file.output_name);
            return 1;
        }
    } else {
        output = stdout;
    }

    if (context.target == TARGET_PREPROCESS) {
        if (context.generate_dependencies && output == stdout) {
            output = NULL;
        }
        preprocess(output);
    } else {
        set_compile_target(output, file.name);
        push_scope(&ns_ident);
        push_scope(&ns_tag);
        register_builtin_declarations();
        push_optimization(optimization_level);

        while ((def = parse()) != NULL) {
            if (context.errors) {
                error("Aborting because of previous %s.",
                    (context.errors > 1) ? "errors" : "error");
                break;
            }

            optimize(def);
            compile(def);
        }

        while ((sym = yield_declaration(&ns_ident)) != NULL) {
            declare(sym);
        }

        if (dump_symbols) {
            output_symbols(stdout, &ns_ident);
            output_symbols(stdout, &ns_tag);
        }

        flush();
        pop_optimization();
        clear_types(dump_types ? stdout : NULL);
        pop_scope(&ns_tag);
        pop_scope(&ns_ident);
    }

    if (context.generate_dependencies) {
        if (file.makefile_name) {
            makefile = fopen(file.makefile_name, "w");
            write_makefile(makefile, file.makefile_target, file.name);
            fclose(makefile);
        } else {
            write_makefile(stdout, file.makefile_target, file.name);
        }
    }

    if (output && output != stdout) {
        fclose(output);
    }

    return context.errors;
}

int main(int argc, char *argv[])
{
    int i, ret;
    struct input_file file;

    if ((ret = parse_program_arguments(argc, argv)) != 0) {
        goto end;
    }

    add_include_search_paths();
    for (i = 0, ret = 0; i < array_len(&input_files); ++i) {
        file = array_get(&input_files, i);
        if ((ret = process_file(file)) != 0) {
            goto end;
        }
    }

    if (context.target == TARGET_x86_64_EXE) {
        ret = invoke_linker();
    }

end:
    finalize();
    parse_finalize();
    preprocess_finalize();
    clear_predefined_macros();
    clear_input_files();
    clear_linker_args();
    return ret < 0 ? 0 : ret;
}
