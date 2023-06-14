#!/usr/bin/env python3
# coding: utf-8
#
# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause

"""
Top-level configuration file for Gunyah build system.

This module constructs an instance of AbstractBuildGraph, and passes it to the
real build system which is in tools/build.

The AbstractBuildGraph class provides an interface which can be used to
declare a build graph consisting of template rules, targets which are build
using those rules, and variables that are substituted into rule commands and
subsequent variable definitions. Implementations of this interface are
provided for Ninja and SCons.

If run as a standalone script, this module generates a Ninja rules file. If
called from a SConstruct or SConscript, it sets up a SCons build that has the
same semantics as the Ninja build. The SConstruct or SConscript will typically
contain the following code:

    import configure
    env = Environment(tools={}, SCANNERS=[], BUILDERS={}, ENV={...})
    configure.SConsBuild(env, Builder, Action, arguments=ARGUMENTS)(...)
"""


import os
import sys
import abc
import re
import runpy
import json


class ClangCompDB(object):

    def __init__(self, path, var_subst):
        self.path = path
        self.var_subst = var_subst
        self.commands = []

    def add_command(self, command, i, o, **local_env):
        self.commands.append({
            'directory': os.getcwd(),
            'command': command,
            'file': i,
            'output': o,
            'local_env': dict(local_env),
        })

    def finalise(self):
        for c in self.commands:
            c['command'] = self.var_subst(c['command'], **c['local_env'])
            del c['local_env']
        d = os.path.dirname(self.path)
        if d:
            try:
                os.makedirs(d)
            except OSError as e:
                import errno
                if e.errno != errno.EEXIST:
                    raise
        with open(self.path, 'w') as f:
            json.dump(self.commands, f)


class AbstractBuildGraph(object):

    __metaclass__ = abc.ABCMeta

    def __init__(self, _parent=None, arguments=None, build_dir=None):
        self._variants = []
        if _parent is None:
            self._is_variant = False
            self._arguments = {} if arguments is None else dict(arguments)
            self._build_dir = 'build' if build_dir is None else build_dir
            self._env = {}
            self._compdbs = {}
            self._rule_compdbs = {}
            self._rule_commands = {}
            self._rule_byproducts = {}
            self.add_gen_source(__file__)
        else:
            self._is_variant = True
            assert arguments is None
            self._arguments = _parent._arguments
            assert build_dir is not None
            self._build_dir = build_dir
            # Make local copies of the parent's environment and rules.
            self._env = dict(_parent._env)
            self._compdbs = dict(_parent._compdbs)
            self._rule_compdbs = dict(_parent._rule_compdbs)
            self._rule_commands = dict(_parent._rule_commands)
            self._rule_byproducts = dict(_parent._rule_byproducts)

    def __call__(self, **kwargs):
        for k in kwargs.keys():
            self.add_env(k, kwargs[k], replace=True)

        if not self._variants:
            try:
                runpy.run_module("tools.build", init_globals={'graph': self})
            except SystemExit as e:
                if e.code:
                    raise

        for c in self._compdbs.values():
            c.finalise()

    def get_argument(self, key, default=None):
        """
        Return the value of a command-line argument, or the specified default.
        """
        return self._arguments.get(key, default)

    @abc.abstractproperty
    def root_dir(self):
        """
        The root directory from which build commands are run.

        This is either absolute, relative to the root directory of the
        repository, or empty. It should be useda as the start= argument of an
        os.path.relpath() call for any path that is specified on a command
        line outside of the ${in} or ${out} variables (e.g. include search
        directories).
        """
        raise NotImplementedError

    @property
    def build_dir(self):
        """
        The base directory that should be used for build outputs.

        This is always a path relative to the working directory.
        """
        return self._build_dir

    def add_variant(self, build_dir):
        """
        Create a variant build, and return the build object.

        This may be called once or more before calling the object itself. If it
        is called at least once before calling the object, the parent build
        will not generate any build rules of its own; instead it will be
        configured to only execute its own variants. If the build generator
        calls this itself, it is responsible for making the distinction
        between variant builds and the top level.

        The specified directory is used as the build directory for the
        variant.
        """
        variant = self._make_variant(build_dir)
        self._variants.append(variant)
        return variant

    @abc.abstractmethod
    def _make_variant(self, build_dir):
        # Create a new build object for a variant based on this one.
        #
        # This is the default implementation, but it is likely to need to be
        # overridden, so subclasses must explicitly call the default through
        # super if they want to use it.
        return type(self)(build_dir=build_dir, _parent=self)

    @abc.abstractmethod
    def add_env(self, name, value, replace=False):
        """
        Add an environment variable.

        The value will be automatically substituted in future value arguments
        to add_env() and command arguments to add_target(), if named in those
        arguments in sh style, i.e. $name or ${name}.

        If the optional replace argument is true, then replacing an existing
        variable is allowed; otherwise it will raise KeyError.
        """
        self._env[name] = self._var_subst(value)

    @abc.abstractmethod
    def append_env(self, name, value, separator=' '):
        """
        Append to an environment variable.

        This is like add_env(), except that if the variable is already set,
        the given value will be appended to it. By default the values are
        separated by spaces, but the optional separator argument can be used
        to replace this.
        """
        if name in self._env:
            self._env[name] += separator
        else:
            self._env[name] = ''
        self._env[name] += self._var_subst(value)

    @abc.abstractmethod
    def get_env(self, name):
        """
        Fetch an environment variable.

        This will return the value of the named environment variable, which
        may have been either set by add_env() or append_env(), or else passed
        to the build system from the external environment.

        If the named value is unknown, this method throws KeyError.
        """
        return self._env[name]

    @abc.abstractmethod
    def add_rule(self, name, command, depfile=None, depfile_external=False,
                 compdbs=None, restat=False):
        """
        Add a build rule.

        The rule name must be unique, and must be a valid Python identifier
        that does not begin with an underscore.

        The command will be run to build targets that use this rule. The
        target name will be substituted for $out or ${out}, and the space
        separated input names will be substituted for $in or ${in}.

        If depfile is set, then it is assumed to be the name of a
        Makefile-style dependency file produced as a side-effect of running
        the command, and will be read (if it exists) to detect implicit
        dependencies (included headers, etc). The target name will be
        substituted for $out or ${out} in this name.

        If the depfile is not generated by the commands in the rule it self,
        then depfile_external should be set to true, otherwise the depfile will
        be added to the list of byproducts.

        If compdbs is set to a list of targets, then targets using this rule
        will be added to the compilation databases represented by those
        targets. The targets must be compilation databases created by calling
        add_compdb().
        """
        compdbs = self._expand_target_list(compdbs)
        rule_compdbs = []
        for c in compdbs:
            if c not in self._compdbs:
                raise KeyError("Not a compdb target: {:s}".format(c))
            rule_compdbs.append(self._compdbs[c])
        if rule_compdbs:
            self._rule_compdbs[name] = tuple(rule_compdbs)
        self._rule_commands[name] = command
        if depfile and not depfile_external:
            self._rule_byproducts[name] = depfile

    @abc.abstractmethod
    def add_target(self, targets, rule, sources=None, depends=None,
                   requires=None, byproducts=None, always=False, **local_env):
        """
        Build one or more targets using a previously created build rule.

        The named rule must be one that has previously been set up with
        add_rule(). That rule's command will be invoked with the given target
        and sources.

        The targets, sources, depends, requires and byproducts arguments are
        all lists of file paths relative to the top level build directory. The
        depends and requires lists may contain names of alias targets, created
        with the add_alias() method; otherwise, all elements of these lists
        must be regular files that either exist in the source tree or will be
        created during the build. If any of these arguments is specified as a
        string, it is treated as a whitespace-separated list.

        The targets are files created by the rule. These must be regular
        files; directories are not allowed. If any target is in a directory
        other than the top-level build directory, then that directory will be
        automatically created before the rule is run. When the rule is run,
        the list of targets will be substituted for the ${out} variable in the
        rule command.

        The listed sources are added as explicit input dependencies and are
        substituted for the ${in} variable in the rule command. Like the
        targets, this may be either a single list

        If a depends list is provided, it specifies additional implicit
        dependencies of this target. These behave the same as sources, except
        that they are not included in the substitution of ${in}.

        If a requires list is provided, it specifies order-only dependencies
        of this target. These are dependencies that are not named on the
        command line, and will not trigger a rebuild if they are newer than
        one of the targets.

        If a byproducts list is provided, it specifies additional products of
        compilation that are generated along with the primary target. These
        behave the same as targets, except that they are not included in the
        substitution of ${out}.

        If the "always" keyword is set to True, the target will be rebuilt
        every time it is used as a dependency.

        Any other keyword arguments are added temporarily to the environment
        while building this specific target, overriding any variables
        currently in the environment. Variable expansion is performed on the
        values in this dictionary. Variables may be appended by expanding
        their previous value in the new value. However, do not locally
        override a variable if its value is substituted in local overrides of
        _other_ variables; the effect of doing so is unspecified, and may vary
        between runs of an otherwise unchanged build.
        """
        sources = self._expand_target_list(sources)
        targets = self._expand_target_list(targets)
        local_env = {
            name: self._var_subst(value) for name, value in local_env.items()
        }
        local_env['in'] = ' '.join(getattr(n, 'abspath', str(n))
                                   for n in sources)
        local_env['out'] = ' '.join(getattr(n, 'abspath', str(n))
                                    for n in targets)
        cmd = self._rule_commands[rule]
        for compdb in self._rule_compdbs.get(rule, ()):
            for s in sources:
                compdb.add_command(cmd, s, targets[0], **local_env)

    @abc.abstractmethod
    def add_alias(self, alias, targets):
        """
        Add an alias (phony) target.

        This method creates a target that does not correspond to a file in the
        build directory, with dependencies on a specific list of other
        targets. It may be used to create aliases like "all", "install", etc.,
        which may then be named on the command line, as default targets, or as
        dependencies, like any other target.

        However, due to a misfeature in SCons, if you need to name an alias in
        a dependency list before defining it, you must wrap the alias's name
        with a call to the future_alias() method.
        """
        raise NotImplementedError

    def future_alias(self, alias):
        """
        Get a reference to an alias that may not have been defined yet.

        If it is necessary to name an alias in a dependency list prior to
        defining it, you must pass the name of the alias to this method and
        add the result to the dependency list. This is because SCons can't
        retroactively change a dependency from a file (the default) to an
        alias.
        """
        return alias

    @abc.abstractmethod
    def add_default_target(self, target, alias=False):
        """
        Add a default target.

        Targets named this way will be built if no target is specified on the
        command line.

        This can be called more than once; the effects are cumulative. If it
        is not called, the fallback is to build all targets that are not used
        as sources for other targets, and possibly also all other targets.
        """
        raise NotImplementedError

    @abc.abstractmethod
    def add_gen_source(self, source):
        """
        Add a generator source.

        Future builds will re-run the generator script if the named file
        changes. The base class calls this for the top-level generator script.
        It may also be called for indirect dependencies of the generator
        (Python modules, configuration files, etc).
        """
        raise NotImplementedError

    @abc.abstractmethod
    def add_gen_output(self, output):
        """
        Add a generator output.

        The generator script may produce additional outputs that build commands
        depend on. By declaring these outputs, the generator script can be
        re-run if the named file is missing or out of date.
        """
        raise NotImplementedError

    def add_compdb(self, target, form='clang'):
        """
        Add a compilation database target.

        If a type is specified, it is the name of one of the supported forms
        of compilation database:

        * 'clang' for Clang JSON

        The default is 'clang'.

        If a rule is attached to this compdb target, then all targets built
        using that rule will be written into the database file.

        This target becomes an implicit output of the build graph generation.
        """
        if form == 'clang':
            compdb = ClangCompDB(target, self._var_subst)
        else:
            raise NotImplementedError("Unknown compdb form: " + repr(form))
        self._compdbs[target] = compdb

    def _expand_target_list(self, target_list):
        """
        This is used to preprocess lists of targets, sources, etc.
        """
        if target_list is None:
            return ()
        elif isinstance(target_list, str):
            return tuple(target_list.split())
        else:
            return tuple(target_list)

    def _var_subst(self, s, **local_env):
        def shrepl(match):
            name = match.group(2) or match.group(3)
            if name in local_env:
                return local_env[name]
            try:
                return self.get_env(name)
            except KeyError:
                return ''
        shvars = re.compile(r'\$((\w+)\b|{(\w+)})')
        n = 1
        while n:
            s, n = shvars.subn(shrepl, s)
        return s


class NinjaBuild(AbstractBuildGraph):
    def __init__(self, ninja_file, **kwargs):
        self._lines = ['# Autogenerated, do not edit']
        self._gen_sources = set()
        self._gen_outputs = set()
        self._env_names = set()
        self._rule_names = {'phony'}
        self._ninja_file = ninja_file
        self._subninja_files = []

        rules_dir = os.path.dirname(ninja_file) or '.'
        try:
            os.makedirs(rules_dir)
        except FileExistsError:
            pass
        self._mkdir_cache = {'.', rules_dir}
        self._mkdir_targets = []

        super(NinjaBuild, self).__init__(**kwargs)

    def _make_variant(self, build_dir):
        ninja_file = os.path.join(build_dir, 'rules.ninja')
        self._subninja_files.append(ninja_file)
        self._lines.append('')
        self._lines.append('subninja ' + ninja_file)

        variant = type(self)(ninja_file, build_dir=build_dir, _parent=self)

        # Shadowed state
        variant._env_names = set(self._env_names)
        variant._rule_names = set(self._rule_names)

        # Shared state
        variant._gen_sources = self._gen_sources

        return variant

    @property
    def _all_ninja_files(self):
        return (self._ninja_file,) + tuple(f for v in self._variants
                                           for f in v._all_ninja_files)

    @property
    def _all_byproducts(self):
        byproducts = tuple(self._compdbs.keys())
        byproducts += tuple(self._gen_outputs)
        byproducts += tuple(f for v in self._variants
                            for f in v._all_byproducts)
        return byproducts

    @property
    def _phony_always(self):
        return os.path.join('tools', 'build', '.should-not-exist')

    def __call__(self, gen_cmd=None, **kwargs):
        super(NinjaBuild, self).__call__(**kwargs)

        if not self._is_variant:
            # Add a rule at the top level to rerun the generator script
            assert gen_cmd is not None
            self.add_rule('_gen_rules', gen_cmd, generator=True, restat=True)
            self.add_target(self._all_ninja_files, '_gen_rules',
                            depends=sorted(self._gen_sources),
                            byproducts=self._all_byproducts)

            # Add a phony rule for always-built targets
            self.add_alias(self._phony_always, [])

            # Add phony rules for all of the generator sources, so Ninja
            # does not fail if one of them disappears (e.g. if a module
            # is renamed, or an older branch is checked out)
            for f in sorted(self._gen_sources):
                self.add_alias(f, [])

        # Add a rule and targets for all of the automatically created parent
        # directories. We do this in deepest-first order at the end of the
        # build file because ninja -t clean always processes targets in the
        # order they appear, so it might otherwise fail to remove directories
        # that will become empty later.
        self.add_rule('_mkdir', 'mkdir -p ${out}')
        for d in reversed(self._mkdir_targets):
            self.add_target([d], '_mkdir', _is_auto_dir=True)

        # Write out the rules file
        with open(self._ninja_file, 'w') as f:
            f.write('\n'.join(self._lines) + '\n')

    @property
    def root_dir(self):
        """
        The root directory from which build commands are run.

        This is either absolute, relative to the root directory of the
        repository, or empty. It should be used as the start= argument of an
        os.path.relpath() call for any path that is specified on a command
        line outside of the ${in} or ${out} variables (e.g. include search
        directories).

        For Ninja, it is simply the empty string.
        """
        return ''

    def add_env(self, name, value, replace=False):
        if name in self._env_names and not replace:
            raise KeyError("Duplicate definition of env ${name}"
                           .format(name=name))
        super(NinjaBuild, self).add_env(name, value, replace=replace)
        self._env_names.add(name)
        self._lines.append('')
        self._lines.append('{name} = {value}'.format(**locals()))

    def append_env(self, name, value, separator=' '):
        if name in self._env_names:
            self._lines.append('')
            self._lines.append('{name} = ${{{name}}}{separator}{value}'
                               .format(**locals()))
            super(NinjaBuild, self).append_env(name, value, separator)
        else:
            self.add_env(name, value)

    def get_env(self, name):
        try:
            return super(NinjaBuild, self).get_env(name)
        except KeyError:
            return os.environ[name]

    def add_rule(self, name, command, depfile=None, depfile_external=False,
                 compdbs=None, generator=False, restat=False):
        if name in self._rule_names:
            raise KeyError("Duplicate definition of rule {name}"
                           .format(name=name))
        super(NinjaBuild, self).add_rule(name, command, depfile=depfile,
                                         depfile_external=depfile_external,
                                         compdbs=compdbs)
        self._rule_names.add(name)
        self._lines.append('')
        self._lines.append('rule ' + name)
        self._lines.append('    command = ' + command)
        self._lines.append('    description = ' + name + ' ${out}')
        if depfile is not None:
            self._lines.append('    depfile = ' + depfile)
        if generator:
            self._lines.append('    generator = true')
        if restat:
            self._lines.append('    restat = true')

    def add_target(self, targets, rule, sources=None, depends=None,
                   requires=None, byproducts=None, always=False,
                   _is_auto_dir=False, **local_env):
        super(NinjaBuild, self).add_target(
            targets, rule, sources=sources, depends=depends,
            requires=requires, byproducts=byproducts, **local_env)
        targets = self._expand_target_list(targets)
        sources = self._expand_target_list(sources)
        depends = self._expand_target_list(depends)
        requires = self._expand_target_list(requires)
        byproducts = self._expand_target_list(byproducts)

        if rule in self._rule_byproducts:
            depsfile = re.sub(r'\$(out\b|{out})', targets[0],
                              self._rule_byproducts[rule])
            byproducts = byproducts + (depsfile,)

        if not _is_auto_dir:
            # Automatically add a dependency on the parent directory of each
            # target that is not at the top level
            for t in targets:
                target_dir = os.path.dirname(os.path.normpath(t))
                if target_dir:
                    self._mkdir(target_dir)
                    requires = requires + (target_dir,)

        self._lines.append('')
        build_line = 'build ' + ' '.join(targets)
        if byproducts:
            build_line += ' | '
            build_line += ' '.join(byproducts)
        build_line += ' : ' + rule
        if sources:
            build_line += ' '
            build_line += ' '.join(sources)
        if depends:
            build_line += ' | '
            build_line += ' '.join(depends)
        if always:
            build_line += ' ' + self._phony_always + ' '
        if requires:
            build_line += ' || '
            build_line += ' '.join(requires)
        self._lines.append(build_line)

        for name in sorted(local_env.keys()):
            self._lines.append('    {} = {}'.format(name, local_env[name]))

    def add_alias(self, alias, targets):
        targets = self._expand_target_list(targets)
        self._lines.append('')
        self._lines.append('build ' + self._escape(alias) + ' : phony ' +
                           ' '.join(targets))

    def add_default_target(self, target, alias=False):
        self._lines.append('')
        self._lines.append('default ' + self._escape(target))

    def add_gen_source(self, source):
        self._gen_sources.add(os.path.normpath(source))

    def add_gen_output(self, output):
        self._gen_outputs.add(os.path.normpath(output))

    def _mkdir(self, target_dir):
        if target_dir in self._mkdir_cache:
            return
        # Always add parent directories first, if any. This ensures that
        # the _mkdir_targets list is ordered with the deepest directories
        # last.
        parent = os.path.dirname(target_dir)
        if parent:
            self._mkdir(parent)
        self._mkdir_cache.add(target_dir)
        self._mkdir_targets.append(target_dir)

    def _escape(self, path):
        return re.sub(r'([ \n:$])', r'$\1', path)

    def _expand_target_list(self, target_list):
        return tuple(self._escape(s)
                     for s in super(NinjaBuild, self)
                     ._expand_target_list(target_list))


class SConsBuild(AbstractBuildGraph):
    def __init__(self, env, Builder, Action, _parent=None, **kwargs):
        self.env = env
        self.Builder = Builder
        self.Action = Action
        self._rule_depfiles = {}
        self._root_dir = env.Dir('#.')
        if _parent is None:
            self._default_targets = []
        else:
            self._default_targets = _parent._default_targets
        super(SConsBuild, self).__init__(_parent=_parent, **kwargs)

    def __call__(self, **kwargs):
        super(SConsBuild, self).__call__(**kwargs)
        return self._default_targets

    @property
    def root_dir(self):
        """
        The root directory from which build commands are run.

        This is either absolute, relative to the root directory of the
        repository, or empty. It should be useda as the start= argument of an
        os.path.relpath() call for any path that is specified on a command
        line outside of the ${in} or ${out} variables (e.g. include search
        directories).

        For SCons, it is the root-relative path of the current directory.
        """
        return os.path.relpath(self._root_dir.abspath)

    def _make_variant(self, build_dir):
        return type(self)(self.env.Clone(), self.Builder, self.Action,
                          build_dir=build_dir, _parent=self)

    def add_env(self, name, value, replace=False):
        if not replace and name in self.env:
            raise KeyError("Duplicate definition of env ${name}"
                           .format(name=name))
        self.env.Replace(**{name: value})
        super(SConsBuild, self).add_env(name, value, replace=replace)

    def append_env(self, name, value, separator=' '):
        if name in self.env:
            self.env.Append(**{name: separator + value})
        else:
            self.env.Replace(**{name: value})
        super(SConsBuild, self).append_env(name, value, separator)

    def get_env(self, name):
        try:
            return super(SConsBuild, self).get_env(name)
        except KeyError:
            return self.env['ENV'][name]

    def add_rule(self, name, command, depfile=None, depfile_external=False,
                 compdbs=None, restat=False):
        if 'Rule_' + name in self.env['BUILDERS']:
            raise KeyError("Duplicate definition of rule {name}"
                           .format(name=name))
        super(SConsBuild, self).add_rule(name, command, depfile=depfile,
                                         depfile_external=depfile_external,
                                         compdbs=compdbs)
        # Replace the Ninja-style $in/$out variables with $SOURCES / $TARGETS
        command = re.sub(r'\$(in\b|{in})', '${SOURCES}', command)
        command = re.sub(r'\$(out\b|{out})', '${TARGETS}', command)
        description = name + ' ${TARGETS}'
        builder = self.Builder(action=self.Action(command, description))
        self.env.Append(BUILDERS={'Rule_' + name: builder})
        if depfile is not None:
            self._rule_depfiles[name] = depfile

    def add_target(self, targets, rule, sources=None, depends=None,
                   requires=None, byproducts=None, always=None, **local_env):
        super(SConsBuild, self).add_target(
            targets, rule, sources=sources, depends=depends,
            requires=requires, byproducts=byproducts, **local_env)
        targets = self._expand_target_list(targets)
        sources = self._expand_target_list(sources)
        depends = self._expand_target_list(depends)
        requires = self._expand_target_list(requires)
        byproducts = self._expand_target_list(byproducts)

        if rule in self._rule_byproducts:
            depsfile = re.sub(r'\$(out\b|{out})', targets[0],
                              self._rule_byproducts[rule])
            byproducts = byproducts + (depsfile,)

        tnodes = getattr(self.env, 'Rule_' + rule)(
            target=targets, source=sources, **local_env)
        if depends:
            self.env.Depends(tnodes, depends)
        if requires:
            self.env.Requires(tnodes, requires)
        if byproducts:
            self.env.SideEffect(byproducts, targets)
            # side-effects are not cleaned by default
            self.env.Clean(targets, byproducts)
        if always:
            self.env.AlwaysBuild(targets)
        if rule in self._rule_depfiles:
            depfile = re.sub(r'\$(out\b|{out})', targets[0],
                             self._rule_depfiles[rule])
            # Note: this is slightly broken; if the depfile is created by the
            # rule that it affects, SCons will spuriously rebuild everything
            # that uses it on the _second_ run after a clean. This appears to
            # be a deliberate feature; the SCons maintainers are ideologically
            # opposed to compiler generated depfiles. Ninja handles them
            # correctly.
            saved_dir = self.env.fs.getcwd()
            try:
                # Change to the root directory, so the depends in the depfile
                # will be interpreted relative to it.
                self.env.fs.chdir(self._root_dir, change_os_dir=False)
                # Note that depfile must be a plain path, not a File node, and
                # SCons will directly call open() on it. So it must be
                # relative to the repository, not to self._root_dir.
                self.env.ParseDepends(depfile)
            finally:
                self.env.fs.chdir(saved_dir, change_os_dir=False)

    def add_alias(self, alias, targets):
        targets = self._expand_target_list(targets)
        self.env.Alias(alias, targets)

    def future_alias(self, alias):
        return self.env.Alias(alias)

    def add_default_target(self, target, alias=False):
        if not alias:
            try:
                target = self.env.Entry(target)
            except ValueError:
                pass
        self.env.Default(target)
        self._default_targets.append(target)

    def add_gen_source(self, source):
        # Don't care about these, SCons regenerates on every run anyway
        pass

    def add_gen_output(self, output):
        # Don't care about these, SCons regenerates on every run anyway
        pass


if __name__ == '__main__':
    # Called stand-alone; generate a Ninja file.
    import pipes
    build = NinjaBuild('build.ninja',
                       arguments=dict(a.split('=', 1) for a in sys.argv[1:]))
    build(gen_cmd=' '.join((pipes.quote(arg) for arg in sys.argv)))
