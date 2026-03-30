# coding: utf-8
#
# Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: BSD-3-Clause

"""
Gunyah build system.

This module is invoked by configure.py with the global variable `graph` set to
an instance of AbstractBuildGraph, which can be used to add rules, targets and
variables to the build graph.
"""

import os
import sys
import logging
import re
from collections import namedtuple
from io import open

from ..utils.genfile import GenFile


try:
    FileNotFoundError
except NameError:
    FileNotFoundError = IOError

# Silence flake8 warnings about the externally-defined graph variable
graph = graph  # noqa: F821


logging.basicConfig()
logger = logging.getLogger(__name__)

true_strings = ('true', 't', '1', 'yes', 'y')
false_strings = ('false', 'f', '0', 'no', 'n')


#
# General setup
#
def relpath(path):
    return os.path.relpath(path, start=graph.root_dir)


class Items(dict):
    def add(self, key, value):
        if key not in self:
            super(Items, self).__setitem__(key, value)
        else:
            raise KeyError("Item duplicate {:s}".format(key))


class module_set(set):
    def __init__(self):
        self.module_re = re.compile(
            r'[A-Za-z][A-Za-z0-9_]*([/][A-Za-z][A-Za-z0-9_]*)*')
        super(module_set, self).__init__()

    def add(self, value):
        if not self.module_re.fullmatch(value):
            print("invalid module name:", value)
            sys.exit(1)
        super(module_set, self).add(value)


build_dir = graph.build_dir

config_base = 'config'
module_base = 'hyp'
arch_base = os.path.join(module_base, 'arch')
interface_base = os.path.join(module_base, 'interfaces')
map_file = os.path.join(os.getcwd(), build_dir, 'hyp.elf.map')

conf_includes = set()
modules = module_set()
modules.add('arch')

interfaces = set()
objects = set()
objects_ast_json = set()
external_objects = set()
guestapis = set()
types = set()
hypercalls = set()
registers = set()
test_programs = set()
sa_html = set()
asts = set()
defmap_frags = set()
deferred_aliases = []
guestapi_only = False
uses_interface_gen_dir = False

bsp_dirs_arg = graph.get_argument('bsp_dirs', '')
bsp_dirs = tuple(bsp_dirs_arg.split(',')) if bsp_dirs_arg else ()

# check for whether need to generate unreachable functions list
gen_callgraph = graph.get_argument(
    "callgraph", 'false').lower() in true_strings


def deferred_alias(alias):
    dep_list = []
    future_alias = graph.future_alias(alias)
    deferred_aliases.append((future_alias, dep_list))
    return (future_alias, dep_list)


event_sources = set()
modules_with_events = set()
interfaces_with_events = set()

template_engines = {}
TemplateEngine = namedtuple('TemplateEngine', ['sources', 'config'])

simple_templates = list()

template_engines['simple'] = \
    TemplateEngine(simple_templates, None)

first_class_objects = Items()
first_class_templates = list()

template_engines['first_class_object'] = \
    TemplateEngine(first_class_templates, None)

typed_templates = list()

template_engines['typed'] = \
    TemplateEngine(typed_templates, None)

typed_guestapi_templates = list()

template_engines['typed_guestapi'] = \
    TemplateEngine(typed_guestapi_templates, None)

hypercalls_templates = list()

template_engines['hypercalls'] = \
    TemplateEngine(hypercalls_templates, None)

registers_templates = list()

template_engines['registers'] = \
    TemplateEngine(registers_templates, None)


shvars_re = re.compile(r'\$((\w+)\b|{(\w+)})')


def var_subst(w):
    def shrepl(match):
        name = match.group(2) or match.group(3)
        try:
            return graph.get_env(name)
        except KeyError:
            logger.error("Undefined environment variable: $%s", name)
            sys.exit(1)
    n = 1
    while n:
        w, n = shvars_re.subn(shrepl, w)
    return w


#
# Variant setup
#
def arch_match(arch_name):
    if not target_arch_names:
        raise KeyError("Cannot match arch in quality config")
    return arch_name in target_arch_names


def quality_match(quality_name):
    return quality_name in quality_aliases


def process_variant_conf(variant_key, conf, basename):
    quality = variant_key == 'quality'
    platform = variant_key == 'platform'
    featureset = variant_key == 'featureset'
    allow_arch = variant_key and not platform

    if platform and basename is not None:
        if basename in target_arch_names:
            logger.error("existing arch: %s", basename)
        target_arch_names.append(basename)

    if quality and basename is not None:
        quality_aliases.append(basename)

    for src_dir in bsp_dirs:
        conf_path = os.path.join(src_dir, conf)
        if os.path.isfile(conf_path):
            break
    else:
        src_dir = '.'
        conf_path = conf

    graph.add_gen_source(conf_path)

    with open(conf_path, 'r', encoding='utf-8') as f:
        for s in f.readlines():
            words = s.split()
            if not words or words[0].startswith('#'):
                # Skip comments or blank lines
                pass
            elif words[0] == 'include':
                include_conf = os.path.join(config_base,
                                            words[1] + '.inc')
                if include_conf not in conf_includes:
                    process_variant_conf(variant_key, include_conf, None)
                    conf_includes.add(include_conf)
            elif quality and words[0] == 'alias_quality':
                quality_aliases.extend(words[1:])
            elif featureset and words[0] == 'platforms':
                featureset_platforms.update(words[1:])
            elif platform and words[0] == 'base_arch':
                arch_conf = os.path.join(config_base, 'arch',
                                         words[1] + '.conf')
                process_variant_conf(variant_key, arch_conf, words[1])
            elif platform and words[0] == 'includes':
                d = os.path.join(src_dir, arch_base, basename, 'include')
                graph.append_env('CPPFLAGS', '-I ' + relpath(d))
            elif platform and words[0] == 'alias_arch':
                if words[1] in target_arch_names:
                    logger.error("Alias existing arch: %s",
                                 words[1])
                target_arch_names.append(words[1])
            elif platform and words[0] == 'is_abi':
                global abi_arch
                if abi_arch is not None:
                    logger.warning("Duplicate abi definitions: %s and %s",
                                   abi_arch, basename)
                abi_arch = basename
            elif platform and words[0] == 'defines_link':
                global linker_script
                linker_script = os.path.join(src_dir, arch_base, basename,
                                             'link.lds')
            elif platform and words[0] == 'target_triple':
                global target_triple
                if target_triple is not None:
                    logger.warning(
                        "Duplicate target triple definitions: %s and %s",
                        target_triple, words[1])
                target_triple = words[1]
            elif words[0] == 'module':
                modules.add(words[1])
            elif words[0] == 'flags':
                variant_cflags.extend(map(var_subst, words[1:]))
            elif words[0] == 'cppflags':
                variant_cppflags.extend(map(var_subst, words[1:]))
            elif words[0] == 'cc_wrapper':
                variant_cc_wrapper.clear()
                variant_cc_wrapper.append(list(map(var_subst, words[1:]))[0])
            elif words[0] == 'ldflags':
                variant_ldflags.extend(map(var_subst, words[1:]))
            elif words[0] == 'configs':
                for c in map(var_subst, words[1:]):
                    add_global_define(c)
            elif words[0] == 'guestapi_only':
                global guestapi_only
                guestapi_only = True
            elif words[0] == 'quality_configs':
                if quality_match(words[1]):
                    for c in map(var_subst, words[2:]):
                        add_global_define(c)
            elif words[0] == 'quality_configs_override':
                if quality_match(words[1]):
                    for c in map(var_subst, words[2:]):
                        add_global_define_override(c)
            elif words[0] == 'quality_flags':
                if quality_match(words[1]):
                    variant_cflags.extend(map(var_subst, words[2:]))
            elif words[0] == 'arch_configs_override':
                if arch_match(words[1]):
                    for c in map(var_subst, words[2:]):
                        add_global_define_override(c)
            elif allow_arch and words[0] == 'arch_module':
                if arch_match(words[1]):
                    modules.add(words[2])
            elif allow_arch and words[0] == 'arch_flags':
                if arch_match(words[1]):
                    variant_cflags.extend(map(var_subst, words[2:]))
            elif allow_arch and words[0] == 'arch_ldflags':
                if arch_match(words[1]):
                    variant_ldflags.extend(map(var_subst, words[2:]))
            elif allow_arch and words[0] == 'arch_configs':
                if arch_match(words[1]):
                    for c in map(var_subst, words[2:]):
                        add_global_define(c)
            else:
                # TODO: dependencies, configuration variables, etc
                # Restructure this to use a proper parser first
                logger.error('Unknown token "%s" in %s', words[0], conf)
                sys.exit(1)


all_arg = graph.get_argument('all', 'false').lower()
if all_arg in true_strings:
    default_all_variants = True
elif all_arg in false_strings:
    default_all_variants = False
else:
    logger.error("Argument all= must have a boolean value, not '%s'", all_arg)
    sys.exit(1)

missing_variant = False
abi_arch = None
linker_script = None
target_triple = None
target_arch_names = []
quality_aliases = []
variant_cc_wrapper = []
variant_cflags = []
variant_cppflags = []
variant_defines = {}
variant_ldflags = []

featureset_platforms = set()

#
# Configs sanity checking
#
seen_configs = {}


def check_global_define(d):
    try:
        define, val = d.split('=')
    except ValueError:
        logger.warning("invalid configuration: %s", d)

    if define in seen_configs:
        if seen_configs[define] == val:
            logger.warning("Duplicate configuration: %s", d)
        else:
            logger.error("Conflicting configuration: %s and %s",
                         '='.join([define, seen_configs[define]]), d)
            sys.exit(-1)
    seen_configs[define] = val


def check_global_define_override(d):
    try:
        define, val = d.split('=')
    except ValueError:
        logger.warning("invalid configuration: %s", d)

    if define in seen_configs:
        # logger.warning("Duplicate configuration: %s", d)
        seen_configs[define] = val
    else:
        logger.error("config doesn't exit so cannot override %s", d)
        sys.exit(-1)


def add_global_define(d):
    check_global_define(d)
    define, val = d.split('=')
    variant_defines[define] = val


def add_global_define_override(d):
    check_global_define_override(d)
    define, val = d.split('=')
    variant_defines[define] = val


for variant_key in ('quality', 'platform', 'featureset'):
    try:
        variant_value = graph.get_env('VARIANT_' + variant_key)
    except KeyError:
        variant_arg = graph.get_argument(
            variant_key, 'all' if default_all_variants else None)

        import glob
        known_variants = frozenset(
            os.path.splitext(os.path.basename(f))[0]
            for d in bsp_dirs + ('.',)
            for f in glob.iglob(os.path.join(
                d, config_base, variant_key, '*.conf')))
        if not known_variants:
            logger.error('No variants known for key %s', variant_key)
            sys.exit(1)

        if variant_arg is None:
            logger.error('No variant specified for key %s; choices: %s',
                         variant_key, ', '.join(known_variants))
            missing_variant = True
            continue

        if variant_arg == 'all':
            selected_variants = known_variants
        else:
            selected_variants = frozenset(variant_arg.split(','))
            if not (selected_variants <= known_variants):
                logger.error("Unknown variants specified for key %s: %s; "
                             "choices: %s", variant_key,
                             ', '.join(selected_variants - known_variants),
                             ', '.join(known_variants))
                missing_variant = True
                continue

        for val in selected_variants:
            graph.add_variant(os.path.join(build_dir, val))(**{
                'VARIANT_' + variant_key: val
            })

        # Don't build anything until all variants are configured
        sys.exit()

    variant_conf = os.path.join(config_base, variant_key,
                                variant_value + '.conf')
    process_variant_conf(variant_key, variant_conf, variant_value)

if featureset_platforms and ('*' not in featureset_platforms) and \
        (graph.get_env('VARIANT_platform') not in featureset_platforms):
    # Skip platforms not supported in the featureset
    sys.exit(0)

if missing_variant:
    sys.exit(1)

for a in target_arch_names:
    graph.append_env('CODEGEN_ARCHS', '-a ' + a)


try:
    partial_link_arg = graph.get_env('PARTIAL_LINK')
except KeyError:
    partial_link_arg = graph.get_argument('partial_link', '0')
do_partial_link = partial_link_arg.lower() in true_strings

try:
    sa_enabled_arg = graph.get_env('ENABLE_SA')
except KeyError:
    sa_enabled_arg = graph.get_argument('enable_sa', '0')
do_sa_html = sa_enabled_arg.lower() in true_strings


#
# Match available template generators
#


def template_match(template_engine, d):
    try:
        return template_engines[template_engine]
    except KeyError:
        logger.error('Unknown template system "%s" in %s', template_engine, d)
        sys.exit(1)


#
# Architecture setup
#

# Add the arch generic include directory for asm-generic/ headers
graph.append_env('CPPFLAGS', '-I ' + os.path.join(
    relpath(arch_base), 'generic', 'include'))


# Set up for a freestanding ARMv8.2 EL2 target
graph.append_env('TARGET_CFLAGS', '-ffreestanding')
graph.append_env('TARGET_CFLAGS', '-ftls-model=local-exec')
graph.append_env('TARGET_CFLAGS', '-fpic')
if not do_partial_link:
    graph.append_env('TARGET_LDFLAGS', '-pie')

# Enable stack protection by default
graph.append_env('TARGET_CFLAGS', '-fstack-protector-strong')


#
# Toolchain setup
#

graph.add_env('ROOT_DIR', os.path.realpath(graph.root_dir))
graph.add_env('BUILD_DIR', os.path.realpath(build_dir))

try:
    llvm_root = graph.get_env('LLVM')
except KeyError:
    try:
        llvm_root = graph.get_env('QCOM_LLVM')
    except KeyError:
        logger.error(
            "Please set $QCOM_LLVM or $LLVM to the root of the prebuilt LLVM")
        sys.exit(1)

# Use a QC prebuilt LLVM
graph.add_env('CLANG', os.path.join(llvm_root, 'bin', 'clang'))
graph.add_env('CLANG_MAP', os.path.join(
    llvm_root, 'bin', 'clang-extdef-mapping'))

graph.add_env('FORMATTER',
              '{:s} --style=file:{:s}'.format(
                  os.path.join(llvm_root, 'bin', 'clang-format'),
                  relpath('.clang-format')))

# Use Clang to compile.
graph.add_env('TARGET_TRIPLE', target_triple)
if variant_cc_wrapper:
    graph.add_env('TARGET_CC', ' '.join(variant_cc_wrapper))
    graph.append_env('TARGET_CC', '${CLANG} -target ${TARGET_TRIPLE}')
else:
    graph.add_env('TARGET_CC', '${CLANG} -target ${TARGET_TRIPLE}')

unreach_func_gen_script = os.path.join(
    'tools', 'cpptest', 'get_unreachable_functions.py')
graph.add_env('GEN_UNREACHABLE_FUNCTIONS', relpath(unreach_func_gen_script))

# LDFLAGS to create crt map file
graph.append_env("LDFLAGS", '-Wl,-Map,' + map_file)


# Use Clang with LLD to link.
graph.add_env('TARGET_LD', '${TARGET_CC} -fuse-ld=lld')

# Use Clang to preprocess DSL files.
graph.add_env('CPP', '${CLANG}-cpp -target ${TARGET_TRIPLE}')

# Use C18. For the purposes of MISRA, the language is C99 and all differences
# between C99 and C18 are language extensions permitted by a project deviation
# from rule 1.2.
graph.append_env('CFLAGS', '-std=gnu18')
# Turn all warnings on as errors by default
graph.append_env('CFLAGS', '-Weverything')
graph.append_env('CFLAGS', '-Werror')
# Permit disabling warnings added in future compiler releases
graph.append_env('CFLAGS', '-Wno-unknown-warning-option')
# Unused macros are expected
graph.append_env('CFLAGS', '-Wno-unused-macros')
# MISRA rule 16.4 requires default: in every switch, even if it is covered
graph.append_env('CFLAGS', '-Wno-covered-switch-default')
# No need for C++ compatibility
graph.append_env('CFLAGS', '-Wno-c++98-compat')
graph.append_env('CFLAGS', '-Wno-c++-compat')
# No need for pre-C11 compatibility, we always use C18
graph.append_env('CFLAGS', '-Wno-pre-c11-compat')
graph.append_env('CFLAGS', '-Wno-declaration-after-statement')
# No need for GCC compatibility
graph.append_env('CFLAGS', '-Wno-gcc-compat')
# Allow GCC's _Alignof(lvalue) as a project deviation from MISRA rule 1.2.
graph.append_env('CFLAGS', '-Wno-gnu-alignof-expression')
# Allow GCC's case ranges as a project deviation from MISRA rule 1.2. These
# are also a proposed extension for C2y, so disable that warning too.
graph.append_env('CFLAGS', '-Wno-gnu-case-range')
graph.append_env('CFLAGS', '-Wno-c2y-extensions')
# Allow Clang nullability as a project deviation from MISRA rule 1.2.
graph.append_env('CFLAGS', '-Wno-nullability-extension')
# Automatically requiring negative capabilities breaks analysis of reentrant
# locks, like the preemption count.
graph.append_env('CFLAGS', '-Wno-thread-safety-negative')
# The unsafe buffer usage check is impractical for C code at present. We may be
# able to enable it in future if we generate and call bounds-checked accessors
# for every buffer.
graph.append_env('CFLAGS', '-Wno-unsafe-buffer-usage')

# We depend on section garbage collection; otherwise there are undefined and
# unused symbols that will be pulled in and cause link failures
graph.append_env('CFLAGS', '-ffunction-sections')
graph.append_env('CFLAGS', '-fdata-sections')
if not do_partial_link:
    graph.append_env('LDFLAGS', '-Wl,--gc-sections')

# Ensure that there are no symbol clashes with externally linked objects.
graph.append_env('CFLAGS', '-fvisibility=hidden')

# Generate DWARF compatible with older T32 releases
graph.append_env('CFLAGS', '-gdwarf-4')

# Catch undefined switches during type system preprocessing
graph.append_env('CPPFLAGS', '-Wundef')
graph.append_env('CPPFLAGS', '-Werror')

# Add the variant-specific flags
if variant_cflags:
    graph.append_env('CFLAGS', ' '.join(variant_cflags))
if variant_cppflags:
    graph.append_env('CPPFLAGS', ' '.join(variant_cppflags))
if variant_ldflags:
    graph.append_env('TARGET_LDFLAGS', ' '.join(variant_ldflags))

# On scons builds, the abs path may be put into the commandline, strip it out
# of the __FILE__ macro.
root = os.path.abspath(os.curdir) + os.sep
graph.append_env('CFLAGS',
                 '-fmacro-prefix-map={:s}={:s}'.format(root, ''))

graph.append_env('TARGET_CPPFLAGS', '-nostdlibinc')
graph.append_env('TARGET_LDFLAGS', '-nostdlib')
graph.append_env('TARGET_LDFLAGS', '-Wl,-z,max-page-size=0x1000')
graph.append_env('TARGET_LDFLAGS', '-Wl,-z,notext')

# Build rules
compdb_file = os.path.join(build_dir, 'compile_commands.json')
graph.add_compdb(compdb_file, form='clang')
# Compile a target C file.
graph.add_rule('cc',
               '$TARGET_CC $CFLAGS $CPPFLAGS $TARGET_CFLAGS '
               '$TARGET_CPPFLAGS $LOCAL_CFLAGS $LOCAL_CPPFLAGS -MD -MF '
               '${out}.d -c -o ${out} ${in}',
               depfile='${out}.d', compdbs=[compdb_file])
# Generate a JSON format AST dump for each source file. The dumps are
# very large so we gzip them.
graph.add_rule('cc_ast_json',
               '$TARGET_CC $CFLAGS $CPPFLAGS $TARGET_CFLAGS '
               '$TARGET_CPPFLAGS $LOCAL_CFLAGS $LOCAL_CPPFLAGS '
               ' -fsyntax-only -Xclang -ast-dump=json ${in} '
               '| gzip -9 > ${out}')
# Generate callgraph from AST dumps and then use it to find out a list
# of unreachable functions. Output is generated in the format expected
# by Parasoft tools.
graph.add_rule('gen_unreachable_psrc',
               'python $GEN_UNREACHABLE_FUNCTIONS ${in} -m $MAP -o ${out}')
# Preprocess a DSL file.
graph.add_rule('cpp-dsl', '${CPP} $CPPFLAGS $TARGET_CPPFLAGS $LOCAL_CPPFLAGS '
               '-undef $DSL_DEFINES -x c -P -MD -MF ${out}.d -MT ${out} '
               '${in} > ${out}',
               depfile='${out}.d')
# Link a target binary.
graph.add_rule('ld', '$TARGET_LD $LDFLAGS $TARGET_LDFLAGS $LOCAL_LDFLAGS '
               '${in} -o ${out}')


# Directory to hold all the .ast files for CTU analysis
ctu_dir = os.path.join(build_dir, "ctu")
graph.add_env('CTU_DIR', relpath(ctu_dir))


# Map the source paths to their corresponding .ast locations under CTU_DIR
ctu_src_map = [
    (os.path.abspath(os.getcwd()), '.'),
    (os.path.abspath(graph.build_dir), 'build'),
]
for (i, bsp) in enumerate(bsp_dirs):
    ctu_src_map.append((os.path.abspath(bsp), 'bsp' + str(i)))

ctu_map_sed = '; '.join(r's| \+{:s}/| {:s}/|g'.format(*m) for m in ctu_src_map)
graph.add_env('CTU_MAP_SED', ctu_map_sed)

# CTU rule to generate the .ast files
graph.add_rule('cc-ctu-ast',
               '$TARGET_CC $CFLAGS $CPPFLAGS $TARGET_CFLAGS $TARGET_CPPFLAGS '
               '$LOCAL_CFLAGS $LOCAL_CPPFLAGS -DCLANG_CTU_AST '
               '-D__clang_analyzer__=1 '
               '-MD -MF ${out}.d -Wno-unused-command-line-argument '
               '-emit-ast -o${out} ${in}',
               depfile='${out}.d')

graph.add_env('COMPDB_DIR', compdb_file)

# CTU rule to generate the externalDefMap files
graph.add_rule('cc-ctu-map',
               '$CLANG_MAP -p $COMPDB_DIR ${in} | '
               'sed -e "s/\\$$/.ast/g; ${CTU_MAP_SED}" > '
               '${out}')

graph.add_rule('cc-ctu-all', 'cat ${in} > ${out}')

# Run the static analyzer
graph.add_rule('cc-analyze',
               '$TARGET_CC $CFLAGS $CPPFLAGS $TARGET_CFLAGS $TARGET_CPPFLAGS '
               '$LOCAL_CFLAGS $LOCAL_CPPFLAGS '
               '-Wno-unused-command-line-argument '
               '--analyze '
               '-Xanalyzer -analyzer-output=html '
               '-Xanalyzer -analyzer-config '
               '-Xanalyzer experimental-enable-naive-ctu-analysis=true '
               '-Xanalyzer -analyzer-config '
               '-Xanalyzer stable-report-filename=true '
               '-Xanalyzer -analyzer-config '
               '-Xanalyzer unroll-loops=true '
               '-Xanalyzer -analyzer-config '
               '-Xanalyzer ctu-dir=$CTU_DIR '
               '-Xanalyzer -analyzer-disable-checker '
               '-Xanalyzer core.FixedAddressDereference '
               '-Xanalyzer -analyzer-disable-checker '
               '-Xanalyzer security.ArrayBound '
               '-o ${out} '
               '${in}')


#
# Parse the module configurations
#
def process_dir(d, handler):
    conf = os.path.join(d, 'build.conf')

    with open(conf, 'r', encoding='utf-8') as f:
        handler(d, f)
        graph.add_gen_source(conf)


def module_build_path(d, obj_type):
    return os.path.join(build_dir, obj_type,
                        os.path.basename(os.path.dirname(d)),
                        os.path.basename(d))


def module_gen_path(d):
    return module_build_path(d, 'autogen')


def module_obj_path(d):
    return module_build_path(d, 'obj')


def module_local_headers_target(d):
    return os.path.join(module_gen_path(d), 'local_headers_gen')


def parse_module_conf(d, f):
    local_env = {}
    module = os.path.basename(d)
    local_headers_gen, local_headers = \
        deferred_alias(module_local_headers_target(d))
    add_include_dir(get_event_local_inc_dir(module), local_env)
    src_requires = (
        hyptypes_header,
        version_header,
        sym_version_header,
        registers_header,
        typed_headers_gen,
        event_headers_gen,
        hypercalls_headers_gen,
        interface_headers_gen,
        local_headers_gen,
    )
    have_events = False

    # Use a queue to process all config lines, including included ones
    from collections import deque
    config_queue = deque(f.readlines())
    processed_includes = set()  # Prevent circular includes

    while config_queue:
        s = config_queue.popleft()
        words = s.split()
        if not words or words[0].startswith('#'):
            # Skip comments or blank lines
            pass
        elif words[0] == 'interface':
            for w in map(var_subst, words[1:]):
                # Temporarily exclude legacy interface duplicates
                # FIXME: QC Gunyah issue #261
                if w in interfaces and w not in ['platform', 'smccc',
                                                 'watchdog', 'psci']:
                    raise KeyError(
                        "duplicate interface: {:s}, in {}".format(w, f.name))
                interfaces.add(w)
        elif words[0] == 'types':
            for w in map(var_subst, words[1:]):
                types.add(add_type_dsl(d, w, local_env))
        elif words[0] == 'hypercalls':
            for w in map(var_subst, words[1:]):
                hypercalls.add(add_hypercall_dsl(d, w, local_env))
        elif words[0] == 'events':
            for w in map(var_subst, words[1:]):
                event_sources.add(add_event_dsl(d, w, local_env))
                have_events = True
        elif words[0] == 'registers':
            for w in map(var_subst, words[1:]):
                registers.add(add_register_dsl(d, w, local_env))
        elif words[0] == 'local_include':
            add_include(d, 'include', local_env)
        elif words[0] == 'source':
            for w in map(var_subst, words[1:]):
                add_source(d, w, src_requires, local_env)
        elif words[0] == 'external_object':
            if not do_partial_link:
                for w in map(var_subst, words[1:]):
                    external_objects.add(w)
        elif words[0] == 'flags':
            add_flags(map(var_subst, words[1:]), local_env)
        elif words[0] == 'configs':
            for c in map(var_subst, words[1:]):
                add_global_define(c)
        elif words[0] == 'macros':
            for w in map(var_subst, words[1:]):
                add_macro_include(d, 'include', w)
        elif words[0] == 'arch_types':
            if arch_match(words[1]):
                for w in map(var_subst, words[2:]):
                    types.add(add_type_dsl(
                        d, os.path.join(words[1], w), local_env))
        elif words[0] == 'arch_hypercalls':
            if arch_match(words[1]):
                for w in map(var_subst, words[2:]):
                    f = os.path.join(words[1], w)
                    hypercalls.add(add_hypercall_dsl(d, f, local_env))
        elif words[0] == 'arch_events':
            if arch_match(words[1]):
                for w in map(var_subst, words[2:]):
                    event_sources.add(add_event_dsl(
                        d, os.path.join(words[1], w), local_env))
                    have_events = True
        elif words[0] == 'arch_registers':
            if arch_match(words[1]):
                for w in map(var_subst, words[2:]):
                    registers.add(add_register_dsl(
                        d, os.path.join(words[1], w), local_env))
        elif words[0] == 'arch_local_include':
            if arch_match(words[1]):
                add_include(d, os.path.join(words[1], 'include'), local_env)
        elif words[0] == 'arch_source':
            if arch_match(words[1]):
                for w in map(var_subst, words[2:]):
                    add_source(os.path.join(d, words[1]),
                               w, src_requires, local_env)
        elif words[0] == 'arch_external_object':
            if not do_partial_link:
                if arch_match(words[1]):
                    for w in map(var_subst, words[2:]):
                        external_objects.add(w)
        elif words[0] == 'arch_flags':
            if arch_match(words[1]):
                add_flags(map(var_subst, words[2:]), local_env)
        elif words[0] == 'arch_configs':
            if arch_match(words[1]):
                for w in map(var_subst, words[2:]):
                    add_global_define(w)
        elif words[0] == 'arch_macros':
            if arch_match(words[1]):
                for w in map(var_subst, words[2:]):
                    add_macro_include(d, 'include', w)
        elif words[0] == 'first_class_object':
            first_class_objects.add(words[1], words[2:])
        elif words[0] == 'base_module':
            for w in map(var_subst, words[1:]):
                # Require the base module's generated headers
                module_dir = find_module_dir(w)
                local_headers.append(graph.future_alias(
                    module_local_headers_target(w)))
                module_gen_dir = module_gen_path(w)
                # FIXME: We can't properly determine whether there are
                # local_includes or not unless we do two-pass parsing of the
                # build configs, so we just add them all.
                logger.disabled = True
                add_include(module_dir, 'include', local_env)
                add_include(module_gen_dir, 'include', local_env)
                # FIXME: We assume module has all possible arch include dirs
                for arch_name in target_arch_names:
                    arch_dir = os.path.join(arch_name, 'include')
                    add_include(module_dir, arch_dir, local_env)
                    add_include(module_gen_dir, arch_dir, local_env)
                logger.disabled = False
                modules.add(os.path.relpath(w, module_base))
                if module_dir not in module_dirs:
                    module_dirs.append(module_dir)
        elif words[0] == 'template':
            ts = template_match(words[1], d)
            for w in map(var_subst, words[2:]):
                if add_template(ts, d, '', w, src_requires, local_env,
                                module, local_headers):
                    have_events = True
        elif words[0] == 'arch_template':
            ts = template_match(words[1], d)
            if arch_match(words[2]):
                for w in map(var_subst, words[3:]):
                    if add_template(ts, d, words[2], w, src_requires,
                                    local_env, module, local_headers):
                        have_events = True
        elif words[0] == 'assert_config':
            test = ' '.join(words[1:])

            try:
                result = eval(
                    test, {'configs': variant_defines}, configs_as_ints)
                if result is True:
                    continue
            except (NameError, SyntaxError) as e:
                print('eval error:', e)
                pass
            print('with:')
            print('\tplatform:', graph.get_env('VARIANT_platform'))
            print('\tfeatureset:', graph.get_env('VARIANT_featureset'))
            print('\tquality:', graph.get_env('VARIANT_quality'))
            logger.error('assert_config failed "%s" in module conf for %s',
                         test, d)
            sys.exit(1)
        elif words[0] == 'build_include':
            inc_dir = words[1]
            inc_build_conf = os.path.join(d, inc_dir, 'build.conf')

            # Prevent circular includes
            abs_inc_path = os.path.abspath(inc_build_conf)
            if abs_inc_path in processed_includes:
                logger.error(
                    'Circular build_include detected: %s', inc_build_conf)
                sys.exit(1)

            if not os.path.exists(inc_build_conf):
                logger.error('build_include file: %s is missing',
                             inc_build_conf)
                sys.exit(1)

            processed_includes.add(abs_inc_path)
            graph.add_gen_source(inc_build_conf)

            with open(inc_build_conf, 'r', encoding='utf-8') as inc:
                inc_lines = inc.readlines()
                # Add to front of queue for immediate processing
                config_queue.extendleft(reversed(inc_lines))
        else:
            # TODO: dependencies, configuration variables, etc
            # Restructure this to use a proper parser first
            logger.error('Unknown token "%s" in module conf for %s',
                         words[0], d)
            sys.exit(1)
    if have_events:
        local_headers.append(get_event_local_inc_file(module))
        modules_with_events.add(module)
        add_event_handlers(module)


def parse_interface_conf(d, f):
    local_env = {}
    interface = os.path.basename(d)
    have_events = False

    for s in f.readlines():
        words = s.split()
        if not words or words[0].startswith('#'):
            # Skip comments or blank lines
            pass
        elif words[0] == 'includes':
            inc_dir = os.path.join(d, 'include')
            graph.append_env('CPPFLAGS', '-I ' + relpath(inc_dir))
        elif words[0] == 'types':
            for w in map(var_subst, words[1:]):
                types.add(add_type_dsl(d, w, local_env))
        elif words[0] == 'hypercalls':
            for w in map(var_subst, words[1:]):
                hypercalls.add(add_hypercall_dsl(d, w, local_env))
        elif words[0] == 'events':
            for w in map(var_subst, words[1:]):
                event_sources.add(add_event_dsl(d, w, local_env))
                have_events = True
        elif words[0] == 'registers':
            for w in map(var_subst, words[1:]):
                registers.add(add_register_dsl(d, w, local_env))
        elif words[0] == 'macros':
            for w in map(var_subst, words[1:]):
                add_macro_include(d, 'include', w)
        elif words[0] == 'arch_includes':
            for arch in words[1:]:
                if arch_match(arch):
                    inc_dir = os.path.join(interface_base, interface, arch,
                                           'include')
                    graph.append_env('CPPFLAGS', '-I ' + relpath(inc_dir))
        elif words[0] == 'arch_types':
            if arch_match(words[1]):
                for w in map(var_subst, words[2:]):
                    types.add(add_type_dsl(
                        d, os.path.join(words[1], w), local_env))
        elif words[0] == 'arch_hypercalls':
            if arch_match(words[1]):
                for w in map(var_subst, words[2:]):
                    f = os.path.join(words[1], w)
                    hypercalls.add(add_hypercall_dsl(d, f, local_env))
        elif words[0] == 'arch_events':
            if arch_match(words[1]):
                for w in map(var_subst, words[2:]):
                    event_sources.add(add_event_dsl(
                        d, os.path.join(words[1], w), local_env))
                    have_events = True
        elif words[0] == 'arch_macros':
            if arch_match(words[1]):
                for w in map(var_subst, words[2:]):
                    add_macro_include(os.path.join(d, words[1], 'include', w))
        elif words[0] == 'first_class_object':
            first_class_objects.add(words[1], words[2:])
        elif words[0] == 'template':
            ts = template_match(words[1], d)
            for w in map(var_subst, words[2:]):
                if add_template(ts, d, '', w, None, local_env, None, None):
                    have_events = True
        else:
            # TODO: dependencies, configuration variables, etc
            # Restructure this to use a proper parser first
            logger.error('Unknown token "%s" in interface conf for %s',
                         words[0], d)
            sys.exit(1)
    if have_events:
        interfaces_with_events.add(interface)
        add_event_handlers(interface)


def add_include_dir(d, local_env):
    if not d.startswith(build_dir):
        if not os.path.isdir(d):
            logger.warning("include path: '{:s}' non-existent!".format(d))
    if 'LOCAL_CPPFLAGS' in local_env:
        local_env['LOCAL_CPPFLAGS'] += ' '
    else:
        local_env['LOCAL_CPPFLAGS'] = ''
    local_env['LOCAL_CPPFLAGS'] += '-iquote ' + relpath(d)


def add_include(module_dir, include, local_env):
    add_include_dir(os.path.join(module_dir, include), local_env)


def add_flags(flags, local_env):
    if 'LOCAL_CFLAGS' in local_env:
        local_env['LOCAL_CFLAGS'] += ' '
    else:
        local_env['LOCAL_CFLAGS'] = ''
    local_env['LOCAL_CFLAGS'] += ' '.join(flags)


def add_source_file(src, obj, requires, local_env):
    file_env = local_env.copy()
    if 'LOCAL_CPPFLAGS' not in file_env:
        file_env['LOCAL_CPPFLAGS'] = ''
    else:
        file_env['LOCAL_CPPFLAGS'] += ' '

    graph.add_target([obj], 'cc', [src], requires=requires,
                     **file_env)
    objects.add(obj)

    if do_sa_html and src.endswith(".c"):
        for src_base, ctu_subdir in ctu_src_map:
            if os.path.abspath(src).startswith(src_base):
                ast = os.path.join(ctu_dir, ctu_subdir,
                                   os.path.relpath(src + '.ast', src_base))
                break
        else:
            logger.error('No ctu map matching %s', src)
            sys.exit(1)
        graph.add_target([ast], 'cc-ctu-ast', [src], requires=requires,
                         **file_env)
        asts.add(ast)

        defmap_frag = os.path.join(ctu_dir, src + ".map")
        graph.add_target([defmap_frag], 'cc-ctu-map', [src], requires=requires,
                         **file_env)
        defmap_frags.add(defmap_frag)

        sa_html_dir = obj + ".html"
        graph.add_target([sa_html_dir], 'cc-analyze', [src], requires=requires,
                         depends=(ast_gen, defmap), **file_env)
        sa_html.add(sa_html_dir)

    if gen_callgraph and src.endswith(".c"):
        ast_json = obj + '.ast.json.gz'
        graph.add_target([ast_json], 'cc_ast_json', [src], requires=requires,
                         depends=[obj], **file_env)
        objects_ast_json.add(ast_json)


def add_source(module_dir, src, requires, local_env):
    if not src.endswith(".c") and not src.endswith(".S"):
        logger.error('unknown source file type for: %s', src)
        sys.exit(1)
    out_dir = module_obj_path(module_dir)
    i = os.path.join(module_dir, 'src', src)
    o = os.path.join(out_dir, src + '.o')
    add_source_file(i, o, requires, local_env)
    return o


def add_macro_include(module_dir, include, src):
    graph.append_env('CPPFLAGS', '-imacros {:s}'
                     .format(relpath(os.path.join(module_dir, include, src))))


def add_preproc_dsl(module_dir, src, **local_env):
    out_dir = module_gen_path(module_dir)
    i = os.path.join(module_dir, src)
    o = os.path.join(out_dir, src + '.pp')
    graph.add_target([o], 'cpp-dsl', [i], **local_env)
    return o


def add_type_dsl(module_dir, src, local_env):
    return add_preproc_dsl(module_dir, src, DSL_DEFINES='-D__TYPED_DSL__',
                           **local_env)


def add_hypercall_dsl(module_dir, src, local_env):
    return add_preproc_dsl(module_dir, src, DSL_DEFINES='-D__HYPERCALLS_DSL__',
                           **local_env)


def add_event_dsl(module_dir, src, local_env):
    return add_preproc_dsl(module_dir, src, requires=(hypconstants_header,),
                           DSL_DEFINES='-D__EVENTS_DSL__', **local_env)


def add_register_dsl(module_dir, src, local_env):
    return add_preproc_dsl(module_dir, src, **local_env)


def add_template(ts, d, arch, tmpl_file, requires, local_env, module, headers):
    ext = os.path.splitext(tmpl_file)[1]
    is_module = module is not None

    if ext == '.h':
        if is_module:
            mod_gen_dir = os.path.join(module_gen_path(d), arch)
            add_include(mod_gen_dir, 'include', local_env)
        else:
            assert headers is None
            headers = interface_gen_headers

    if ext == '.c' and not is_module:
        logger.error('C template specified for interface %s', d)
        sys.exit(1)

    ts.sources.append((d, tmpl_file, arch, requires, is_module, local_env,
                       headers))
    return ext == '.ev'


event_handler_modules = set()


def add_event_handlers(module):
    if module in event_handler_modules:
        return
    event_handler_modules.add(module)
    obj = get_event_src_file(module) + '.o'
    event_src_requires = (
        hyptypes_header,
        typed_headers_gen,
        interface_headers_gen,
        get_event_inc_file(module),
    )
    add_source_file(get_event_src_file(module), obj, event_src_requires, {})


# Header locations
interface_gen_dir = os.path.join(build_dir, 'interface', 'include')
objects_build_dir = os.path.join(build_dir, 'objects')
events_inc_dir = os.path.join(build_dir, 'events', 'include')

# Autogenerated interface headers
interface_headers_gen, interface_gen_headers = deferred_alias(
    os.path.join(build_dir, 'interface_headers_gen'))

# Support for the event generator
graph.append_env('CPPFLAGS', '-I ' + relpath(events_inc_dir))
event_headers_gen, event_headers = deferred_alias(
    os.path.join(build_dir, 'event_headers_gen'))

# Support for the hypercalls generator
hypercalls_headers_gen, hypercalls_headers = deferred_alias(
    os.path.join(build_dir, 'hypercalls_headers_gen'))


def get_event_local_inc_dir(module):
    return os.path.join(build_dir, 'events', module, 'include')


def get_event_local_inc_file(module):
    return os.path.join(get_event_local_inc_dir(module), 'event_handlers.h')


def get_event_inc_file(module):
    return os.path.join(events_inc_dir, 'events', module + '.h')


def get_event_src_file(module):
    return os.path.join(build_dir, 'events', 'src', module + '.c')


#
# Global generated headers depends
#
build_includes = os.path.join(build_dir, 'include')
hyptypes_header = os.path.join(build_includes, 'hyptypes.h')
hypconstants_header = os.path.join(build_includes, 'hypconstants.h')
registers_header = os.path.join(build_includes, 'hypregisters.h')
version_header = os.path.join(build_includes, 'hypversion.h')
sym_version_header = os.path.join(build_includes, 'hypsymversion.h')
graph.append_env('CPPFLAGS', '-I ' + relpath(build_includes))
typed_headers_gen = graph.future_alias(
    os.path.join(build_dir, 'typed_headers_gen'))

guestapi_interface_types = os.path.join(build_dir, 'guestapi', 'include',
                                        'guest_types.h')
#
# Hypercalls generated files
#
# FIXME: This is not hypervisor source, it should not be built.
# Generation temporarily hard coded here until better handling implemented
hypguest_interface_src = os.path.join(build_dir, 'guestapi', 'src',
                                      'guest_interface.c')
hypguest_interface_header = os.path.join(build_dir, 'guestapi', 'include',
                                         'guest_interface.h')

guestapis.add(hypguest_interface_header)
guestapis.add(hypguest_interface_src)

#
# Set up the simple code generator
#
codegen_script = os.path.join('tools', 'codegen', 'codegen.py')
graph.add_env('CODEGEN', relpath(codegen_script))
graph.add_rule('code_gen_c', '${CODEGEN} ${CODEGEN_ARCHS} ${CODEGEN_CONFIGS} '
               '-f "${FORMATTER}" -o ${out} -d ${out}.d ${in}',
               depfile='${out}.d')
graph.add_rule('code_gen', '${CODEGEN} ${CODEGEN_ARCHS} '
               '${CODEGEN_CONFIGS} -o ${out} -d ${out}.d ${in}',
               depfile='${out}.d')


#
# Set up the Clang static analyser
#
defmap = os.path.join(ctu_dir, "externalDefMap.txt")
ast_gen = graph.future_alias(os.path.join(build_dir, 'ast-gen'))

# Get all configs as Ints or strings
configs_as_ints = dict()


def configs_get_int(c):
    try:
        s = variant_defines[c].strip('uU')
        return int(s, 0)
    except ValueError:
        return variant_defines[c]


for c in variant_defines:
    configs_as_ints[c] = configs_get_int(c)

#
# Collect the lists of objects, modules and interfaces
#


def find_module_dir(m, prefix=None):
    d = os.path.join(prefix, m) if prefix else m
    for src_dir in bsp_dirs:
        path = os.path.join(src_dir, d)
        if os.path.isfile(os.path.join(path, 'build.conf')):
            break
    else:
        path = d
    return path


module_dirs = sorted(find_module_dir(m, prefix=module_base) for m in modules)
for d in module_dirs:
    process_dir(d, parse_module_conf)
for i in sorted(interfaces):
    d = find_module_dir(i, prefix=interface_base)
    process_dir(d, parse_interface_conf)


#
# Collect all defines and configs
#
def mkdirs(path):
    try:
        os.makedirs(path)
    except OSError as e:
        import errno
        if e.errno == errno.EEXIST:
            pass
        else:
            raise


define_file = os.path.join(build_dir, 'config.h')
mkdirs(os.path.split(define_file)[0])
graph.add_gen_output(define_file)

with GenFile(define_file, 'w') as f:
    if variant_defines:
        for define_arg in variant_defines.keys():
            f.write(u"#define {:s} {:s}\n".format(
                define_arg, variant_defines[define_arg]))
    for i in sorted(interfaces):
        f.write(u"#define INTERFACE_{:s} 1\n".format(i.upper()))
    for i in sorted(modules):
        i = i.replace(os.path.sep, '_')
        f.write(u"#define MODULE_{:s} 1\n".format(i.upper()))

graph.append_env('CPPFLAGS', '-imacros {:s}'.format(relpath(define_file)))
graph.append_env('CODEGEN_CONFIGS',
                 '-imacros {:s}'.format(relpath(define_file)))


#
# Generate simple templates that only depend on the build configuration
#

for module_dir, target, arch, src_requires, is_module, local_env, headers \
        in simple_templates:
    ext = os.path.splitext(target)[1]
    module = os.path.basename(module_dir)
    template = os.path.join(module_dir, arch, 'templates', target + '.tmpl')
    if ext == '.h':
        if is_module:
            out_dir = os.path.join(
                module_gen_path(module_dir), arch, 'include')
        else:
            assert not arch
            out_dir = interface_gen_dir
            uses_interface_gen_dir = True
        out_source = os.path.join(out_dir, target)
        headers.append(out_source)
    elif ext in ('.c', '.S') and objects is not None:
        out_dir = os.path.join(module_gen_path(module_dir), arch, 'src')
        out_source = os.path.join(out_dir, target)
        out_obj = out_source + '.o'
        add_source_file(out_source, out_obj, src_requires, local_env)
    elif ext == '.ev':
        out_dir = module_gen_path(module_dir)
        out_source = os.path.join(out_dir, target)
        event_sources.add(out_source)
    elif ext == '.tc':
        out_dir = module_gen_path(module_dir)
        out_source = os.path.join(out_dir, target)
        types.add(add_type_dsl(out_dir, target, local_env))
    else:
        logger.error("Unsupported template output: %s", target)
        sys.exit(1)
    graph.add_target([out_source],
                     'code_gen_c' if ext in ('.c', '.h') else 'code_gen',
                     [template], depends=[codegen_script])


# A template produced output in interface_gen_dir; add it to the include path
if uses_interface_gen_dir:
    graph.append_env('CPPFLAGS', '-I ' + relpath(interface_gen_dir))


#
# Generate types and events for first class objects
#

def add_object_c_template(module, template, requires, object_str, target,
                          local_env):
    out = os.path.join(objects_build_dir, module, target)
    graph.add_target([out], 'object_gen_c', [template], OBJ=object_str,
                     depends=[objects_script])
    add_source_file(out, out + '.o', requires, local_env)


def add_object_h_template(module, template, requires, object_str, target,
                          is_module, local_env, headers):
    if is_module:
        out = os.path.join(build_dir, module, 'include', target)
    else:
        out = os.path.join(objects_incl_dir, target)
    headers.append(out)
    graph.add_target([out], 'object_gen_c', [template], OBJ=object_str,
                     depends=[objects_script])


def add_object_event_template(module, template, object_str, target, local_env):
    out_dir = os.path.join(objects_build_dir, module)
    object_ev = os.path.join(out_dir, target)
    graph.add_target([object_ev], 'object_gen', [template], OBJ=object_str,
                     depends=[objects_script])
    event_sources.add(add_event_dsl(out_dir, target, local_env))


def add_object_type_template(module, template, object_str, target, local_env):
    out_dir = os.path.join(objects_build_dir, module)
    object_tc = os.path.join(out_dir, target)
    graph.add_target([object_tc], 'object_gen', [template], OBJ=object_str,
                     depends=[objects_script])
    types.add(add_type_dsl(out_dir, target, local_env))


objects_script = os.path.join('tools', 'objects', 'object_gen.py')
graph.add_env('OBJECTS', relpath(objects_script))
graph.add_rule('object_gen', '${OBJECTS} -t ${in} '
               '${OBJ} -o ${out}')
graph.add_rule('object_gen_c', '${OBJECTS} -t ${in} -f "${FORMATTER}" '
               '${OBJ} -o ${out}')


objects_incl_dir = os.path.join(objects_build_dir, 'include')
fc_objects = []
for x in sorted(first_class_objects):
    fc_objects.append(','.join([x] + first_class_objects[x]))
fc_objects = ' '.join(fc_objects)
have_object_incl = False

for module_dir, target, arch, src_requires, is_module, local_env, headers \
        in first_class_templates:
    ext = os.path.splitext(target)[1]
    module = os.path.basename(module_dir)
    template = os.path.join(module_dir, arch, 'templates', target + '.tmpl')
    if ext == '.ev':
        add_object_event_template(module, template, fc_objects, target,
                                  local_env)
    elif ext == '.tc':
        add_object_type_template(module, template, fc_objects, target,
                                 local_env)
    elif ext == '.c':
        add_object_c_template(module, template, src_requires, fc_objects,
                              target, local_env)
    elif ext == '.h':
        add_object_h_template(module, template, src_requires, fc_objects,
                              target, is_module, local_env, headers)
        if not is_module:
            have_object_incl = True
    else:
        logger.error('Unsupported first_class_object target "%s" in %s',
                     target, module_dir)
        sys.exit(1)

if have_object_incl:
    graph.append_env('CPPFLAGS', '-I ' + relpath(objects_incl_dir))


#
# Setup the types generator
#
types_script = os.path.join('tools', 'typed', 'type_gen.py')
types_pickle = os.path.join(build_dir, 'types.pickle')

graph.add_rule('types_parse', '${TYPED} -a ${ABI} -d ${out}.d '
               '${in} -P ${out}', depfile='${out}.d')
graph.add_target([types_pickle], 'types_parse', sorted(types), ABI=abi_arch)

graph.add_env('TYPED', relpath(types_script))
graph.add_rule('gen_types', '${TYPED} -a ${ABI} -f "${FORMATTER}" -d ${out}.d '
               '-p ${in} -o ${out}', depfile='${out}.d')
graph.add_target(hyptypes_header, 'gen_types', types_pickle, ABI=abi_arch)

# gen guest type
graph.add_rule('gen_public_types',
               '${TYPED} --public -a ${ABI} -f "${FORMATTER}" -d ${out}.d '
               '-p ${in} -o ${out}', depfile='${out}.d')
graph.add_target(guestapi_interface_types, 'gen_public_types',
                 types_pickle, ABI=abi_arch)

graph.add_rule('gen_types_tmpl', '${TYPED} -a ${ABI} '
               '-d ${out}.d -t ${TEMPLATE} -p ${in} -o ${out}',
               depfile='${out}.d')

graph.add_rule('gen_public_types_tmpl', '${TYPED} --public -a ${ABI} '
               '-f "${FORMATTER}" -d ${out}.d -t ${TEMPLATE} '
               '-p ${in} -o ${out}',
               depfile='${out}.d')

typed_headers = []
for module_dir, target, arch, src_requires, is_module, local_env, headers \
        in typed_templates:
    ext = os.path.splitext(target)[1]
    template = os.path.join(module_dir, arch, 'templates', target + '.tmpl')
    if ext == '.h':
        out = os.path.join(build_includes, target)
        typed_headers.append(out)

        graph.add_target([out], 'gen_types_tmpl', types_pickle,
                         depends=[template], TEMPLATE=relpath(template),
                         ABI=abi_arch)
    elif ext == '.c':
        out = os.path.join(module_gen_path(module_dir), 'src', target)

        graph.add_target([out], 'gen_types_tmpl', types_pickle,
                         depends=[template], TEMPLATE=relpath(template),
                         ABI=abi_arch)
        add_source_file(out, out + '.o', src_requires, local_env)
    else:
        logger.error('Unsupported typed_template target "%s" in %s',
                     target, module_dir)
        sys.exit(1)

graph.add_alias(typed_headers_gen, typed_headers)

for module_dir, target, arch, src_requires, is_module, local_env, headers in \
        typed_guestapi_templates:
    assert (is_module)
    ext = os.path.splitext(target)[1]
    template = os.path.join(module_dir, arch, 'templates', target + '.tmpl')
    if ext == '.h':
        subdir = 'include'
    elif ext == '.c':
        subdir = 'src'
    else:
        logger.error('Unsupported typed_guestapi target "%s" in %s',
                     target, module_dir)

    out = os.path.join(build_dir, 'guestapi', subdir, 'guest_' + target)
    graph.add_target([out], 'gen_public_types_tmpl', types_pickle,
                     depends=[template], TEMPLATE=relpath(template),
                     ABI=abi_arch)
    guestapis.add(out)

guestapis.add(guestapi_interface_types)

#
# Setup the hypercalls generator
#
hypercalls_script = os.path.join('tools', 'hypercalls', 'hypercall_gen.py')
graph.add_env('HYPERCALLS', relpath(hypercalls_script))

hypercalls_template_path = os.path.join('tools', 'hypercalls', 'templates')
hypercalls_guest_templates = (('guest_interface.c', hypguest_interface_src),
                              ('guest_interface.h', hypguest_interface_header))

# FIXME: QC Gunyah issue #51
# FIXME: upgrade Lark and remove LANG env workaround.
graph.add_rule('hypercalls_gen', 'LANG=C.UTF-8'
               ' ${HYPERCALLS} -a ${ABI} -f "${FORMATTER}"'
               ' -d ${out}.d -t ${TEMPLATE} -p ${TYPES_PICKLE} ${in}'
               ' -o ${out}', depfile='${out}.d')

for module_dir, target, arch, src_requires, is_module, local_env, headers in \
        hypercalls_templates:
    template = os.path.join(module_dir, arch, 'templates', target + '.tmpl')
    out_ext = os.path.splitext(target)[1]
    if out_ext == '.h':
        out = os.path.join(build_includes, target)
    elif out_ext in ('.c', '.S'):
        out = os.path.join(module_gen_path(module_dir), 'src', target)
    else:
        logger.error("Unsupported template file: %s", target)
        sys.exit(1)
    graph.add_target([out], 'hypercalls_gen', sorted(hypercalls),
                     TEMPLATE=relpath(template), ABI=abi_arch,
                     TYPES_PICKLE=relpath(types_pickle),
                     depends=[types_pickle, template])
    if out_ext == '.h':
        hypercalls_headers.append(out)
    elif out_ext in ('.c', '.S'):
        oo = out + '.o'
        requires = (
            hyptypes_header,
            hypercalls_headers_gen,
            typed_headers_gen,
            interface_headers_gen,
            event_headers_gen,
        )
        add_source_file(out, oo, requires, local_env)

# FIXME: provide a better/standalone way to generate guest headers
for tmpl, out_name in hypercalls_guest_templates:
    template = os.path.join(hypercalls_template_path, tmpl + '.tmpl')
    graph.add_target(out_name, 'hypercalls_gen', sorted(hypercalls),
                     TEMPLATE=relpath(template), ABI=abi_arch,
                     TYPES_PICKLE=relpath(types_pickle),
                     depends=[types_pickle, template])


#
# Setup the events generators
#
def event_template(name):
    return os.path.join('tools', 'events', 'templates', name + '.tmpl')


events_script = os.path.join('tools', 'events', 'event_gen.py')
graph.add_env('EVENTS', relpath(events_script))
event_handlers_tmpl = event_template('handlers.h')
event_triggers_tmpl = event_template('triggers.h')
events_pickle = os.path.join(build_dir, 'events.pickle')
event_src_tmpl = event_template('c')

graph.add_rule('event_parse',
               '${EVENTS} ${INCLUDES} -d ${out}.d ${in} -P ${out}',
               depfile='${out}.d', restat=True)
graph.add_target([events_pickle], 'event_parse', sorted(event_sources))

graph.add_rule('event_gen', '${EVENTS} -t ${TEMPLATE} -m ${MODULE} ${OPTIONS}'
               '${INCLUDES} -d ${out}.d -p ${in} -o ${out}',
               depfile='${out}.d', restat=True)

for module in sorted(interfaces_with_events | modules_with_events):
    event_out = get_event_inc_file(module)
    event_headers.append(event_out)
    graph.add_target([event_out], 'event_gen', events_pickle,
                     MODULE=module, TEMPLATE=relpath(event_triggers_tmpl),
                     depends=[event_triggers_tmpl])
    event_out = get_event_src_file(module)
    graph.add_target([event_out], 'event_gen', events_pickle,
                     MODULE=module, TEMPLATE=relpath(event_src_tmpl),
                     OPTIONS='-f "${FORMATTER}"',
                     depends=[event_src_tmpl])

for module in sorted(modules_with_events):
    # Gen handler headers
    event_out = get_event_local_inc_file(module)
    graph.add_target([event_out], 'event_gen', events_pickle,
                     MODULE=module, TEMPLATE=relpath(event_handlers_tmpl),
                     depends=[event_handlers_tmpl])

# Generate the static analysis definition map and ASTs
graph.add_target([defmap], 'cc-ctu-all', sorted(defmap_frags))
graph.add_alias(ast_gen, sorted(asts))

#
# Generate register accessors
#

registers_script = os.path.join('tools', 'registers', 'register_gen.py')
graph.add_env('REGISTERS', relpath(registers_script))
graph.add_rule('registers_gen',
               '${REGISTERS} -t ${TEMPLATE} -f "${FORMATTER}" '
               '-o ${out} -p ${TYPES_PICKLE} ${in}')

for module_dir, target, arch, src_requires, is_module, local_env, headers in \
        registers_templates:
    template = os.path.join(module_dir, arch, 'templates', target + '.tmpl')

    header = os.path.join(build_includes, target)
    graph.add_target([header], 'registers_gen', sorted(registers),
                     TEMPLATE=relpath(template),
                     TYPES_PICKLE=relpath(types_pickle),
                     depends=[types_pickle, template, registers_script])

for alias, deps in deferred_aliases:
    graph.add_alias(alias, deps)

#
# Build version setup
#
version_module_dir = find_module_dir('core/version', prefix=module_base)
version_file = os.path.join(version_module_dir, 'include', 'version.h')

if os.path.exists(version_file):
    graph.add_rule('version_copy', 'cp ${in} ${out}')
    graph.add_target([version_header], 'version_copy', [version_file])
else:
    ver_script = os.path.join('tools', 'build', 'gen_ver.py')
    graph.add_rule('version_gen', 'PYTHONPATH=' +
                   relpath(os.path.join('tools', 'utils')) + ' ' +
                   relpath(ver_script) + ' -C ' + relpath('.') +
                   ''.join(f' -O {d:s}' for d in bsp_dirs) +
                   ' -o ${out}', restat=True)
    graph.add_target([version_header], 'version_gen', always=True)

#
# Symbols version setup
#
sym_ver_script = os.path.join('tools', 'build', 'gen_sym_ver.py')
graph.add_rule('sym_version_gen', relpath(sym_ver_script) + ' > ${out}')
graph.add_target([sym_version_header], 'sym_version_gen', always=True)

#
# Top-level targets
#

guestapi_gen = os.path.join(build_dir, 'guestapi_gen')
graph.add_alias(guestapi_gen, sorted(guestapis))
graph.add_default_target(guestapi_gen, True)

if guestapi_only:
    sys.exit(0)

# Run the static analyser if 'enable_sa' is set in command line
if do_sa_html:
    sa_alias = os.path.join(build_dir, 'sa-html')
    graph.add_alias(sa_alias, sa_html)
    graph.add_default_target(sa_alias)

# Pre-process the linker script
linker_script_pp = os.path.join(build_dir, 'link.lds.pp')
graph.add_target([linker_script_pp], 'cpp-dsl',
                 [linker_script], requires=[hypconstants_header])

# Link the hypervisor ELF file
if do_partial_link:
    hyp_elf = os.path.join(build_dir, 'hyp.o')
    graph.append_env('TARGET_LDFLAGS', '-r -Wl,-x')
    graph.add_default_target(linker_script_pp)
else:
    hyp_elf = os.path.join(build_dir, 'hyp.elf')
    graph.append_env('TARGET_LDFLAGS',
                     '-Wl,-T,{:s}'.format(relpath(linker_script_pp)))
graph.add_target([hyp_elf], 'ld', sorted(objects | external_objects),
                 depends=[linker_script_pp], byproducts=map_file)
graph.add_default_target(hyp_elf)

# gen callgraph
if gen_callgraph:
    exclusion_symbol_file = os.path.join(build_dir, 'excludeSymbols.psrc')
    graph.add_target([exclusion_symbol_file], 'gen_unreachable_psrc',
                     sorted(objects_ast_json), MAP=map_file,
                     depends=[map_file])
    graph.add_default_target(exclusion_symbol_file)
