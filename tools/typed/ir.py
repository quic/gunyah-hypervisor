# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# 2019 Cog Systems Pty Ltd.
#
# SPDX-License-Identifier: BSD-3-Clause

import inspect
import math
import itertools
import os
import abc
from Cheetah.Template import Template
from exceptions import DSLError
from collections import namedtuple
from functools import wraps

from lark import Transformer, Tree, Token, Discard

"""
The classes in the module represent the features of the DSL language.  They
form an intermediate representation and are used to generate the output.  An
instance of TopLevel will contain all information necessary to generate the
output.
"""
__loc__ = os.path.realpath(
    os.path.join(
        os.getcwd(),
        os.path.dirname(__file__)))

default_copyright = \
    "© 2021 Qualcomm Innovation Center, Inc. All rights reserved.\n" \
    "SPDX-License-Identifier: BSD-3-Clause"


def property_autoupdate(f):
    @wraps(f)
    def wrapper(self):
        if not self._updated:
            self._autoupdate()
        return f(self)

    return property(wrapper)


class TopLevel:
    def __init__(self):
        self.declarations = []
        self.definitions = []
        # record if need to link type to it's definition
        self.type_refs = []
        self.constant_refs = []
        # record the need to call set_abi() on the type
        self.abi_refs = set()

    def gen_output(self, public_only=False):
        footer = []
        code = []
        extra = []

        sorted_defs = []
        seen = set()

        def visit(d):
            if d in seen:
                return
            seen.add(d)
            for dep in d.dependencies:
                visit(dep)
            sorted_defs.append(d)

        # Sort, ensuring that dependencies come before dependent definitions
        for d in sorted(self.definitions, key=lambda x: x.indicator):
            visit(d)
        assert all(d in self.definitions for d in sorted_defs)
        assert all(d in sorted_defs for d in self.definitions)

        for d in sorted_defs:
            if public_only and not d.is_public:
                continue
            f = d.gen_forward_decl()
            code += f

        if self.definitions:
            code += '\n'

        for d in itertools.chain(sorted_defs, self.declarations):
            if public_only and not d.is_public:
                continue
            c, e = d.gen_code()
            code += c
            extra += e

        code.append('\n')
        code.append('#pragma clang diagnostic push\n')
        code.append('#pragma clang diagnostic ignored "-Wpadded"\n')
        code += PrimitiveType.gen_result_types()
        for d in sorted_defs:
            if public_only and not d.is_public:
                continue
            code += d.gen_result_types()
        code.append('#pragma clang diagnostic pop\n\n')

        # FIXME: move to a template file
        output = "// Automatically generated. Do not modify.\n//\n"
        output += '// ' + '\n// '.join(default_copyright.split('\n')) + '\n\n'
        output += "\n"
        output += "#include <stddef.h>\n"
        output += "#include <stdint.h>\n"
        output += "#include <stdbool.h>\n"
        output += "#include <stdalign.h>\n"
        output += "#include <stdatomic.h>\n"
        # output += "#include <assert.h>\n"
        # output += "#include <string.h>\n"
        output += "#include <stdnoreturn.h>\n"
        output += "#include <hyperror.h>\n"
        output += "\n"
        output += ' '.join(code)
        output += "\n"
        output += ' '.join(extra)
        output += ' '.join(footer)

        return output

    def apply_template(self, template_file, public_only=False):
        ns = [{
            'declarations': tuple(d for d in self.declarations
                                  if d.is_public or not public_only),
            'definitions': tuple(d for d in self.definitions
                                 if d.is_public or not public_only),
        }]
        template = Template(file=template_file, searchList=ns)
        return str(template)

    def merge(self, t):
        # TODO: need to handle all declaration type reference, especially
        # objection
        self.declarations += t.declarations
        self.definitions += t.definitions
        self.type_refs += t.type_refs
        self.constant_refs += t.constant_refs
        for a in t.abi_refs:
            self.abi_refs.add(a)

    def _handle_refs(self, abi):
        # FIXME: check for duplicates
        defs = {(d.type_name, d.category): d for d in self.definitions}

        # Set the ABI for types whose definition depends on it
        for t in self.abi_refs:
            t.set_abi(abi)

        # resolve type and constant references
        for r in itertools.chain(self.type_refs, self.constant_refs):
            k = (r.indicator, r.category)
            if k in defs:
                r.link(defs[k])
            else:
                raise DSLError("Failed to find corresponding definition for " +
                               r.indicator + ", category(" + r.category + ")",
                               r.indicator)

    def update(self, abi):
        """
        Second pass, handle internal information & setup connections between
        nodes inside.
        """
        # Add unreferenced primitives
        refs = set()
        for t in self.abi_refs:
            if t.indicator not in refs:
                refs.add(t.indicator)
        for t in PrimitiveType.c_type_names:
            if t not in refs:
                x = PrimitiveType(t)
                self.abi_refs.add(x)

        # link customised declarations and definitions
        self._handle_refs(abi)

        # trigger the update of definitions and declarations
        for d in itertools.chain(self.declarations, self.definitions):
            d.update()


class ICustomized:
    BITFIELD = "bitfield"
    OBJECT = "object"
    STRUCTURE = "structure"
    UNION = "union"
    ENUMERATION = "enumeration"
    ALTERNATIVE = "alternative"
    IMPORT = "import"
    CONSTANT = "constant"


class ICustomizedReference(ICustomized, metaclass=abc.ABCMeta):

    @abc.abstractmethod
    def link(self, definition):
        """
        Link the definition of the base type to this customized object.
        """
        raise NotImplementedError


class IConstantExpression(metaclass=abc.ABCMeta):
    """
    Interface for integer constant expressions.
    """

    def __init__(self):
        super().__init__()
        self._cache = None

    def __int__(self):
        if self._cache is None:
            self._cache = self.to_int()
        return self._cache

    def __reduce__(self):
        return (int, (int(self),))

    def __format__(self, format_spec):
        return int(self).__format__(format_spec)

    @abc.abstractmethod
    def to_int(self):
        """
        Convert the expression to a constant value.

        The result of this method is cached after it returns a value other
        than None.
        """
        raise NotImplementedError


class ConstantExpression(IConstantExpression):
    """
    Top-level constant expression.
    """

    def __init__(self, expr):
        super().__init__()
        self._cache = None
        self.expr = expr

    def to_int(self):
        return int(self.expr)


class ConstantReference(IConstantExpression, ICustomizedReference):
    """
    Constant reference.
    """

    def __init__(self, symbol_name):
        super().__init__()
        self.referenced = False
        self.symbol_name = symbol_name

    @property
    def indicator(self):
        return self.symbol_name

    def to_int(self):
        if self.referenced:
            raise DSLError("Definition of constant is self-referential",
                           self.indicator)
        self.referenced = True
        return int(self.expr)

    @property
    def category(self):
        return self.CONSTANT

    def link(self, definition):
        self.expr = definition.value


class UnaryOperation(IConstantExpression):
    """
    Apply a unary operator to a constant expression.
    """
    operator_map = {
        '+': lambda x: x,
        '-': lambda x: -x,
        '~': lambda x: ~x,
        '!': lambda x: int(x == 0),
    }

    def __init__(self, operator, expr):
        super().__init__()
        try:
            self.func = self.operator_map[operator]
        except KeyError:
            raise DSLError("Unhandled unary operator", self.operator)
        self.expr = expr

    def to_int(self):
        return self.func(int(self.expr))


class BinaryOperation(IConstantExpression):
    """
    Apply a binary operator to a constant expression.
    """
    operator_map = {
        '+': lambda x, y: x + y,
        '-': lambda x, y: x - y,
        '*': lambda x, y: x * y,
        '/': lambda x, y: x // y,
        '%': lambda x, y: x % y,
        '<<': lambda x, y: x << y,
        '>>': lambda x, y: x >> y,
        '<': lambda x, y: int(x < y),
        '>': lambda x, y: int(x > y),
        '<=': lambda x, y: int(x <= y),
        '>=': lambda x, y: int(x >= y),
        '==': lambda x, y: int(x == y),
        '!=': lambda x, y: int(x != y),
        '&': lambda x, y: x & y,
        '^': lambda x, y: x ^ y,
        '|': lambda x, y: x | y,
        '&&': lambda x, y: int(x and y),
        '||': lambda x, y: int(x or y),
    }

    def __init__(self, operator, left_expr, right_expr):
        super().__init__()
        try:
            self.func = self.operator_map[operator]
        except KeyError:
            raise DSLError("Unhandled binary operator", self.operator)
        self.left_expr = left_expr
        self.right_expr = right_expr

    def to_int(self):
        return self.func(int(self.left_expr), int(self.right_expr))


class ConditionalOperation(IConstantExpression):
    """
    Apply a conditional (ternary) operator to a constant expression.
    """

    def __init__(self, cond_expr, true_expr, false_expr):
        super().__init__()
        self.cond_expr = cond_expr
        self.true_expr = true_expr
        self.false_expr = false_expr

    def to_int(self):
        cond = int(self.cond_expr) != 0
        return int(self.true_expr) if cond else int(self.false_expr)


class TypePropertyOperation(IConstantExpression):
    """
    A constant expression evaluating to some integer property of a type.
    """

    def __init__(self, compound_type):
        super().__init__()
        self.compound_type = compound_type

    def to_int(self):
        try:
            return getattr(
                self.compound_type.basic_type.definition,
                self._type_property)
        except AttributeError:
            return getattr(self.compound_type, self._type_property)


class SizeofOperation(TypePropertyOperation):
    _type_property = 'size'


class AlignofOperation(TypePropertyOperation):
    _type_property = 'alignment'


class MinofOperation(TypePropertyOperation):
    _type_property = 'minimum_value'


class MaxofOperation(TypePropertyOperation):
    _type_property = 'maximum_value'


class IType(metaclass=abc.ABCMeta):
    """
    Interface for all types.
    """

    def __init__(self):
        super().__init__()
        self.qualifiers = set()

    @abc.abstractproperty
    def indicator(self):
        """
        Return the AST node that names the type, for use in error messages.
        """
        raise NotImplementedError

    @abc.abstractproperty
    def is_public(self):
        """
        True if this type is exposed in the public API.
        """
        raise NotImplementedError

    def set_const(self):
        """
        Add a const qualifier, if none already exists.

        This is used on members of aggregate types, which can inherit the
        qualifier from the aggregate.
        """
        if self.is_writeonly:
            raise DSLError(
                "Can't have a constant type with a writeonly member",
                self.name)
        self.qualifiers.add(Qualifier("const"))

    def gen_forward_decl(self):
        """
        Generates a forward declaration (if needed) for the type.
        """
        return ([])

    def gen_expr(self):
        """
        When need to construct C output expressions, it
        returns (left, right) parts, which is use for expression:
        left variable_name right.

        For example, int *a[8];
        left part is "int *", right part is "[8];"
        """
        return ([], [])

    def gen_type(self):
        """
        When need to construct C typdef expressions, if the type is too complex
        to directly use, it returns (left, right) parts, which is use for
        expression:
        typedef left variable_name right.

        For example, typedef int *a[8];
        left part is "int *", right part is "[8];"
        """
        return ([], [])

    @property
    def basic_type(self):
        return self

    @property
    def bitsize(self):
        """
        The size of this type's value in bits.

        Returns None if the true range of the type is not known, which is the
        case for all scalar types other than booleans and enumerations.

        Implemented only for scalar types.
        """
        raise DSLError("Non-scalar type cannot be used in this context",
                       self.indicator)

    @property
    def minimum_value(self):
        """
        The minimum scalar value of this type.

        Implemented only for scalar types.
        """
        bits = self.size * 8 if self.bitsize is None else self.bitsize
        return -1 << (bits - 1) if self.is_signed else 0

    @property
    def maximum_value(self):
        """
        The maximum scalar value of this type.

        Implemented only for scalar types.
        """
        bits = self.size * 8 if self.bitsize is None else self.bitsize
        return (1 << (bits - (1 if self.is_signed else 0))) - 1

    @abc.abstractproperty
    def size(self):
        """
        The size of this type in bytes.
        """
        raise NotImplementedError

    @property
    def alignment(self):
        """
        The alignment of this type in bytes, after qualifiers are applied.
        """
        return max(
            (q.align_bytes for q in self.qualifiers if q.is_aligned),
            default=1 if self.is_packed else self.default_alignment)

    @abc.abstractproperty
    def default_alignment(self):
        """
        The alignment of this type in bytes, if not overridden by a qualifier.
        """
        raise NotImplementedError

    @property
    def is_const(self):
        """
        Return True if current type is a const type.
        """
        return any((q.is_const for q in self.qualifiers))

    @property
    def is_atomic(self):
        """
        Return True if current type is an atomic type.
        """
        return any((q.is_atomic for q in self.qualifiers))

    @property
    def is_writeonly(self):
        """
        Return True if current type is write-only.

        This is only applicable to bitfield members. It suppresses generation
        of a read accessor for the member.
        """
        return any((q.is_writeonly for q in self.qualifiers))

    @property
    def is_packed(self):
        """
        Return True if the current type is packed.

        This only makes sense for aggregate types and members in them.
        """
        return any((q.is_packed for q in self.qualifiers))

    @property
    def is_contained(self):
        """
        Return True if the container_of macro should be generated for the
        current type.
        """
        return any(q.is_contained for q in self.qualifiers)

    @property
    def is_signed(self):
        """
        For scalar types, returns whether the type is signed.

        This is not implemented for non-scalar types.
        """
        raise DSLError("Non-scalar type cannot be used in this context",
                       self.indicator)

    @property
    def accessor_basename(self):
        """
        The name prefix for this type's accessors, if any.

        For aggregate types which generate accessor functions, i.e.
        structures, objects and bitfields, this is the type name. For all
        other types, it is None.
        """
        return None

    @property
    def is_array(self):
        """
        True if this type is an array type.
        """
        return False

    @property
    def is_pointer(self):
        """
        True if this type is a pointer type.
        """
        return False

    @abc.abstractproperty
    def dependencies(self):
        """
        Return all definitions that this type relies on.

        Note, pointers and other non definitions are not considered
        dependencies as they can be forward declared.
        """
        raise NotImplementedError


class ICustomizedType(ICustomizedReference, IType):
    """
    Interface for customized type/symbol like structure, bitfield, object,
    constant, etc.
    """

    def __init__(self, type_suffix=''):
        super().__init__()
        self.definition = None
        self.category = "invalid"
        self.type_name = "invalid"
        # indicate this customized type is used as an array or pointer, so the
        # memory stores multiple element or other type of data instead of a
        # single customized type
        self.complex_type = False
        self.type_suffix = type_suffix

    @property
    def indicator(self):
        return self.type_name

# TODO: separate definitions from extensions list
# turn definitions into a dict, and assert there are no duplicates created
#    c_types = {}
#        if type_name in c_types:
#            raise DSLError("duplicate definition of...")
#
#        IType.c_types[type_name] = self

    def link(self, definition):
        """
        Link the definition with declaration
        """
        self.definition = definition

    @property
    def size(self):
        try:
            return self.definition.size
        except AttributeError:
            raise DSLError(self.type_name + " is not defined", self.type_name)

    @property
    def bitsize(self):
        try:
            return self.definition.bitsize
        except AttributeError:
            raise DSLError(self.type_name + " is not defined", self.type_name)

    @property
    def default_alignment(self):
        try:
            return self.definition.alignment
        except AttributeError:
            raise DSLError(self.type_name + " is not defined", self.type_name)

    @property
    def is_signed(self):
        try:
            return self.definition.is_signed
        except AttributeError:
            raise DSLError(self.type_name + " is not defined", self.type_name)

    @property
    def is_pointer(self):
        try:
            return self.definition.is_pointer
        except AttributeError:
            raise DSLError(self.type_name + " is not defined", self.type_name)

    @property
    def is_array(self):
        try:
            return self.definition.is_array
        except AttributeError:
            raise DSLError(self.type_name + " is not defined", self.type_name)

    @property
    def is_public(self):
        try:
            return self.definition.is_public
        except AttributeError:
            raise DSLError(self.type_name + " is not defined", self.type_name)

    def gen_expr(self):
        return ([self.type_name + self.type_suffix], [";\n"])

    def gen_type(self):
        return ([self.type_name + self.type_suffix], [";"])

    @property
    def dependencies(self):
        """
        Return all definitions that this type relies on.

        Note, pointers and other non definitions are not considered
        dependencies as they can be forward declared.
        """
        return (self.definition,)


class IGenCode(metaclass=abc.ABCMeta):
    """
    Interface for C code generation.

    The interface will return (code, extra).
    Code should contain all declarations and definitions. The extra contains
    all flattened information such as getters/setters
    """

    @abc.abstractmethod
    def gen_code(self):
        return ([], [])


class IAggregation:
    """
    Interface for definition/extension who has declarations & definitions
    """

    def __init__(self):
        super().__init__()
        self.declarations = []
        self.definitions = []
        self.type_refs = []

    def set_declarations(self, declarations):
        self.declarations = declarations

    def set_definitions(self, definitions):
        self.definitions = definitions


class IUpdate():
    """
    Interface for all class who needs the second pass scan
    """

    def update(self):
        """
        Update internal data, prepare to generate code
        """
        pass


class IExtension(ICustomizedReference, IAggregation):
    """
    Interface for extension of bit field or object
    """

    def __init__(self):
        super().__init__()
        self.prefix = None


class ICustomizedDefinition(ICustomized, IType, IAggregation, IUpdate):
    """
    Interface for all customized definition
    """

    def __init__(self):
        super().__init__()
        self.type_name = "invalid"
        self.type_link_cnt = 0
        self._public = False

    @property
    def is_public(self):
        return self._public

    def set_public(self):
        self._public = True

    @property
    def indicator(self):
        return self.type_name

    def gen_result_types(self):
        """
        Generate result and pointer-result wrapper structures for the type.
        """
        return (
            "HYPTYPES_DECLARE_RESULT({:s})\n".format(self.type_name),
            "HYPTYPES_DECLARE_RESULT_PTR({:s})\n".format(self.type_name),
        )


class IDeclaration(IUpdate):
    """
    Interface for all declarations, which add members to compound types.
    """

    BITFIELD = "bitfield"
    OBJECT = "object"
    PRIMITIVE = "primitive"

    SEPARATOR = "_"

    def __init__(self):
        super().__init__()
        self.category = self.PRIMITIVE
        self.member_name = None
        # FIXME: compound_type is badly named; it is really the declared type.
        self.compound_type = None
        # complex type indicates that the actually data stored by this
        # declaration is not just one single element of basic type, instead
        # it has multiple elements or different type of data saved in the
        # memory right now, there's only array and pointer make the type
        # complicated
        self.complex_type = False
        self.is_customized_type = False
        self.is_ignore = False

        # keep the type need to find corresponding definition
        self.type_ref = None

        # indicate which definition owns this declaration
        self.owner = None

    def set_ignored(self):
        assert(not self.is_ignore)
        self.is_ignore = True

    def set_const(self):
        self.compound_type.set_const()

    def get_members(self, prefix=None):
        """
        Return the list of members added to the enclosing aggregate.
        """
        return ((self._get_member_name(prefix), self.compound_type, self),)

    def _get_member_name(self, prefix):
        """
        Get the name of this member, given an optional prefix.
        """
        prefix = '' if prefix is None else prefix + self.SEPARATOR
        member_name = prefix + self.member_name
        return member_name


class PrimitiveType(IType):
    PRIMITIVE = "primitive"
    c_type_names = {
        'bool': 'bool',
        'uint8': 'uint8_t',
        'uint16': 'uint16_t',
        'uint32': 'uint32_t',
        'uint64': 'uint64_t',
        'uintptr': 'uintptr_t',
        'sint8': 'int8_t',
        'sint16': 'int16_t',
        'sint32': 'int32_t',
        'sint64': 'int64_t',
        'sintptr': 'intptr_t',
        'char': 'char',
        'size': 'size_t',
    }
    abi_type_names = {
        'uregister': 'uregister_t',
        'sregister': 'sregister_t',
    }

    def __init__(self, type_name):
        super().__init__()
        self.type_name = type_name
        self.category = self.PRIMITIVE
        assert type_name in itertools.chain(self.c_type_names,
                                            self.abi_type_names)
        if type_name in self.c_type_names:
            self.c_type_name = self.c_type_names[type_name]
        else:
            self.c_type_name = self.abi_type_names[type_name]

    @property
    def indicator(self):
        return self.type_name

    @property
    def is_public(self):
        return True

    def set_abi(self, abi):
        if self.type_name in self.abi_type_names:
            self.c_type_name = abi.get_c_type_name(self.c_type_name)
        ctype = abi.get_c_type(self.c_type_name)
        self._is_signed = ctype.is_signed
        self._size = ctype.size
        self._align = ctype.align
        self._bitsize = ctype.bitsize

    def gen_expr(self):
        return ([self.c_type_name], [";\n"])

    def gen_type(self):
        return ([self.c_type_name], [";"])

    @property
    def size(self):
        return self._size

    @property
    def is_signed(self):
        return self._is_signed

    @property
    def default_alignment(self):
        return self._align

    @property
    def bitsize(self):
        return self._bitsize

    def __repr__(self):
        return "PrimitiveType<{:s}>".format(self.indicator)

    @property
    def dependencies(self):
        """
        Return all definitions that this type relies on.

        Note, pointers and other non definitions are not considered
        dependencies as they can be forward declared.
        """
        return ()

    @classmethod
    def gen_result_types(cls):
        """
        Generate result and pointer-result wrapper structures for all types.
        """
        code = ["\n"]
        for name, c_name in itertools.chain(cls.c_type_names.items()):
            code.append(
                "HYPTYPES_DECLARE_RESULT_({:s}, {:s})\n".format(name, c_name))
            code.append(
                "HYPTYPES_DECLARE_RESULT_PTR_({:s}, {:s})\n"
                .format(name, c_name))
        code.append('HYPTYPES_DECLARE_RESULT_PTR_(void, void)\n')
        return code


class BitFieldType(ICustomizedType):
    def __init__(self, type_name):
        super().__init__('_t')
        self.type_name = type_name
        self.category = self.BITFIELD

    @property
    def accessor_basename(self):
        """
        The name prefix for this type's accessors, if any.

        For aggregate types which generate accessor functions, i.e.
        structures, objects and bitfields, this is the type name. For all
        other types, it is None.
        """
        return self.type_name


class ObjectType(ICustomizedType):
    def __init__(self, type_name):
        super().__init__('_t')
        self.type_name = type_name
        self.category = self.OBJECT

    def link(self, definition):
        super().link(definition)
        if not self.complex_type and definition.type_link_cnt == 0:
            definition.need_export = False
        elif self.complex_type:
            definition.need_export = True
        definition.type_link_cnt += 1

    @property
    def accessor_basename(self):
        """
        The name prefix for this type's accessors, if any.

        For aggregate types which generate accessor functions, i.e.
        structures, objects and bitfields, this is the type name. For all
        other types, it is None.
        """
        return self.type_name


class StructureType(ICustomizedType):
    def __init__(self, type_name):
        super().__init__('_t')
        self.type_name = type_name
        self.category = self.STRUCTURE

    @property
    def accessor_basename(self):
        """
        The name prefix for this type's accessors, if any.

        For aggregate types which generate accessor functions, i.e.
        structures, objects and bitfields, this is the type name. For all
        other types, it is None.
        """
        return self.type_name


class UnionType(ICustomizedType):
    def __init__(self, type_name):
        super().__init__('_t')
        self.type_name = type_name
        self.category = self.UNION

    @property
    def accessor_basename(self):
        """
        The name prefix for this type's accessors, if any.

        For aggregate types which generate accessor functions, i.e.
        structures, objects and bitfields, this is the type name. For all
        other types, it is None.
        """
        return self.type_name


class EnumerationType(ICustomizedType):
    def __init__(self, type_name):
        super().__init__('_t')
        self.type_name = type_name
        self.category = self.ENUMERATION

    @property
    def accessor_basename(self):
        """
        The name prefix for this type's accessors, if any.

        For aggregate types which generate accessor functions, i.e.
        structures, objects and bitfields, this is the type name. For all
        other types, it is None.
        """
        return self.type_name


class AlternativeType(ICustomizedType):
    def __init__(self, type_name):
        super().__init__()
        self.type_name = type_name
        self.category = self.ALTERNATIVE


FieldMap = namedtuple('FieldMap', ['field_bit', 'mapped_bit', 'length'])


class BitFieldMemberMapping:
    """
    Data structure that encodes member's bit mapping to the bitfield
    """

    def __init__(self, shift):
        self.field_shift = shift
        self.field_signed = False
        self.field_maps = []

    def set_signed(self, signed=True):
        self.field_signed = signed

    def add_bit_range(self, field_bit, mapped_bit, length):
        fmap = FieldMap(field_bit, mapped_bit, length)
        self.field_maps.append(fmap)

    def update(self):
        """compact field_maps"""
        i = 0
        try:
            while True:
                a = self.field_maps[i]
                b = self.field_maps[i + 1]
                assert(a.field_bit + a.length == b.field_bit)
                if (a.mapped_bit + a.length == b.mapped_bit):
                    c = FieldMap(a.field_bit, a.mapped_bit,
                                 a.length + b.length)
                    self.field_maps[i] = c
                    del self.field_maps[i + 1]
                else:
                    i += 1
        except IndexError:
            pass

    def __repr__(self):
        ret = "BitFieldMemberMapping<"
        sep = ''
        for x in self.field_maps:
            ret += "{:s}({:d},{:d},{:d})".format(sep, x.field_bit,
                                                 x.mapped_bit, x.length)
            sep = ','
        ret += ">"
        return ret


class BitFieldSpecifier:
    NONE = 0
    RANGE = 1
    OTHERS = 2
    AUTO = 3

    def __init__(self):
        self.specifier_type = self.NONE
        self.bit_length = None
        self.shift = 0
        self.auto_width = None
        self.bit_ranges = []
        self.mapping = None

    def add_bit_range(self, bit, width):
        assert(self.specifier_type in (self.NONE, self.RANGE))
        self.specifier_type = self.RANGE
        self.bit_ranges.append((bit, width))

    def set_type_shift(self, shift):
        assert(self.specifier_type in (self.RANGE, self.AUTO))
        self.shift = shift

    def set_type_auto(self, width=None):
        assert(self.specifier_type is self.NONE)
        self.specifier_type = self.AUTO
        self.auto_width = width

    def set_type_others(self):
        assert(self.specifier_type is self.NONE)
        self.specifier_type = self.OTHERS

    def update_ranges(self, declaration, physical_ranges):
        self.mapping = BitFieldMemberMapping(self.shift)

        # FIXME - reserved members defaults need to be considered
        #       - extended registers need to have reserved ranges / defaults
        #         recalculated
        # if declaration.is_ignore:
        #    print(" - skip", declaration.member_name)
        #    return

        if self.specifier_type is self.RANGE:
            field_bit = self.shift

            for r in reversed(self.bit_ranges):
                if not physical_ranges.insert_range(r, declaration):
                    raise DSLError("bitfield member conflicts with previously "
                                   "specified bits, freelist:\n" +
                                   str(physical_ranges),
                                   declaration.member_name)
                self.mapping.add_bit_range(field_bit, *r)
                field_bit += r[1]
            self.mapping.update()
            self.bit_length = field_bit

    def set_signed(self, signed):
        self.mapping.set_signed(signed)


class DirectType(IType):
    def __init__(self):
        super().__init__()
        self._basic_type = None

    def gen_expr(self):
        le, re = self._basic_type.gen_expr()
        for q in self.qualifiers:
            if q.is_restrict:
                raise DSLError("Restrict qualifier is only for pointer",
                               self._basic_type.indicator)
            le.extend(q.gen_qualifier())
        return (le, re)

    def gen_type(self):
        # FIXME: need to double check how to handle qualifier for template
        l, r = self._basic_type.gen_type()
        return (l, r)

    def set_basic_type(self, type):
        assert self._basic_type is None
        self._basic_type = type

    @property
    def indicator(self):
        return self._basic_type.indicator

    @property
    def basic_type(self):
        return self._basic_type

    @property
    def size(self):
        return self._basic_type.size

    @property
    def bitsize(self):
        return self._basic_type.bitsize

    @property
    def default_alignment(self):
        return self._basic_type.alignment

    @property
    def is_signed(self):
        return self._basic_type.is_signed

    @property
    def is_public(self):
        return self._basic_type.is_public

    @property
    def dependencies(self):
        """
        Return all definitions that this type relies on.

        Note, pointers and other non definitions are not considered
        dependencies as they can be forward declared.
        """
        return self._basic_type.dependencies


class ArrayType(IType):
    def __init__(self, indicator):
        super().__init__()
        self.length = None
        self.base_type = None
        self._indicator = indicator

    @property
    def indicator(self):
        return self._indicator

    def gen_expr(self):
        l, r = self.base_type.gen_expr()
        return (l, ["[{:d}]".format(self.length)] + r)

    def gen_type(self):
        l, r = self.base_type.gen_type()
        return (l, ["[{:d}]".format(self.length)] + r)

    @property
    def size(self):
        return int(self.length) * self.base_type.size

    @property
    def default_alignment(self):
        return self.base_type.alignment

    @property
    def is_array(self):
        """
        True if this type is an array type.
        """
        return True

    @property
    def is_public(self):
        return self.base_type.is_public

    @property
    def dependencies(self):
        """
        Return all other types that this type definition relies on.

        Note, pointers to types are not considered dependencies as they can be
        forward declared.
        """
        return self.base_type.dependencies


class PointerType(IType):
    def __init__(self, indicator):
        super().__init__()
        self.base_type = None
        self.has_pointer = False
        self._indicator = indicator

    @property
    def indicator(self):
        return self._indicator

    def set_abi(self, abi):
        self._size = abi.pointer_size
        self._align = abi.pointer_align

    def gen_expr(self):
        l, r = self.base_type.gen_expr()
        ql = list(itertools.chain(*(
            q.gen_qualifier() for q in self.qualifiers)))

        if self.has_pointer:
            return (l + ["(*"] + ql, [")"] + r)
        else:
            return (l + ["*"] + ql, r)

    def gen_type(self):
        l, r = self.base_type.gen_type()
        return (l + ["*"], r)

    @property
    def size(self):
        return self._size

    @property
    def default_alignment(self):
        return self._align

    @property
    def is_pointer(self):
        return True

    @property
    def is_public(self):
        return self.base_type.is_public

    @property
    def dependencies(self):
        """
        Return all definitions that this type relies on.

        Note, pointers and other non definitions are not considered
        dependencies as they can be forward declared.
        """
        if self.base_type.is_atomic:
            # Clang requires atomic-qualified types to be complete, even when
            # taking a pointer to one. This is not clearly required by the
            # standard and seems to be a Clang implementation quirk.
            return self.base_type.dependencies
        return ()


class PrimitiveDeclaration(IDeclaration):
    """
    Declaration of a single member in a structure or object.
    """

    def __init__(self):
        super().__init__()
        self.category = self.PRIMITIVE
        self.offset = None


class ObjectDeclaration(PrimitiveDeclaration):
    """
    Declaration of an object-typed member, which will be flattened.
    """

    def __init__(self):
        super().__init__()
        self.category = self.OBJECT
        self.noprefix = False

    def get_members(self, prefix=None):
        """
        Return the list of members added to the enclosing aggregate.
        """
        if self.offset is not None:
            raise DSLError("Object-typed member cannot have a fixed offset",
                           self.offset)
        if self.complex_type:
            return super().get_members(prefix=prefix)
        prefix = None if self.noprefix else self._get_member_name(prefix)
        members = tuple(self.type_ref.definition._members(prefix))
        for n, t, d in members:
            if d.offset is not None:
                raise DSLError(
                    "Flattened object type cannot contain fixed offsets",
                    d.member_name)
        return members


templates = {}


class BitFieldDeclaration(IDeclaration):
    """
    Declaration of a field in a BitfieldDefinition.
    """

    ACCESSOR_TEMPLATE = "templates/bitfield-generic-accessor.tmpl"

    def __init__(self):
        super().__init__()
        self.category = self.BITFIELD
        self.prefix = None
        self.bitfield_specifier = None
        self.template = None
        self.bf_type_name = None
        self.unit_type = None
        self.unit_size = -1
        self.ranges = None
        self.default = None

    def gen_code(self):
        # validate parameters
        # XXX
        # if self.bitfield_specifier.sign_map is not None and \
        #   not self.is_signed:
        #    raise DSLError("specified sign map for unsigned type",
        #                   self.member_name)

        body = []
        footer = []

        # generate code to extra

        # FIXME: should be a list of templates (header, c, etc.) ?
        assert self.template is not None

        if 'bitfield' in templates:
            template = templates['bitfield']
        else:
            template = Template.compile(file=open(self.template, 'r',
                                                  encoding='utf-8'))
            templates['bitfield'] = template

        t = template(namespaces=(self))
        footer.append(str(t))

        return (body, footer)

    def get_members(self, prefix=None):
        """
        Return the list of members added to the enclosing aggregate.

        This doesn't make sense for bitfield declarations, which can never
        flatten anything.
        """
        raise NotImplementedError

    def update(self):
        super().update()

        if not self.is_ignore and self.compound_type is None:
            return

        if self.complex_type:
            raise DSLError("cannot declare a complex type in a bitfield",
                           self.member_name)

        b = self.bitfield_specifier

        # Allocate auto bits
        if b.specifier_type is BitFieldSpecifier.AUTO:
            width = b.auto_width
            if width is None:
                if self.compound_type.bitsize is not None:
                    width = self.compound_type.bitsize
                else:
                    width = self.compound_type.size * 8
            else:
                width = int(width)

            r = self.ranges.alloc_range(width, self)
            if r is None:
                raise DSLError("unable to allocate bits", self.member_name)
            assert(r[1] == width)

            b.bit_length = width
            b.mapping.add_bit_range(b.shift, r[0], width)

        assert(b.bit_length is not None)

        b.set_signed(self.compound_type.is_signed)

        const = self.compound_type.is_const
        writeonly = self.compound_type.is_writeonly
        if const and writeonly:
            raise DSLError("const and writeonly is invalid", self.member_name)

        # pdb.set_trace()
        member_typesize = self.compound_type.size * 8

        if member_typesize < b.bit_length:
            raise DSLError(
                "too many bits {:d}, exceed type size {:d}".format(
                    b.bit_length, member_typesize),
                self.member_name)

        member_bitsize = self.compound_type.bitsize

        if member_bitsize is not None and member_bitsize > b.bit_length:
            raise DSLError(
                "not enough bits {:d}, need at least {:d}".format(
                    b.bit_length, member_bitsize),
                self.member_name)

        self.template = os.path.join(__loc__, self.ACCESSOR_TEMPLATE)

    @property
    def indicator(self):
        return self.compound_type.indicator

    @property
    def field_shift(self):
        return self.bitfield_specifier.mapping.field_shift

    @property
    def field_signed(self):
        return self.bitfield_specifier.mapping.field_signed

    @property
    def field_maps(self):
        return self.bitfield_specifier.mapping.field_maps

    @property
    def field_length(self):
        return self.bitfield_specifier.bit_length

    @property
    def is_const(self):
        return self.compound_type.is_const

    @property
    def is_writeonly(self):
        return self.compound_type.is_writeonly

    @property
    def is_bitfield(self):
        return self.compound_type._basic_type.category == 'bitfield'

    @property
    def field_type(self):
        # FIXME: find better solution
        return ' '.join(self.compound_type.gen_type()[0])

    @property
    def field_type_name(self):
        return self.compound_type._basic_type.type_name

    @property
    def field_unit_type(self):
        if self.is_bitfield:
            return self.compound_type._basic_type.definition.unit_type
        else:
            raise TypeError

    @property
    def field_name(self):
        return self._get_member_name(self.prefix)

    def update_ranges(self, ranges):
        self.bitfield_specifier.update_ranges(self, ranges)


class StructurePadding:
    def __init__(self, length):
        super().__init__()
        self._length = length

    def gen_expr(self):
        return (['uint8_t'], ['[{:d}];\n'.format(self._length)])


class StructureDefinition(IGenCode, ICustomizedDefinition):

    def __init__(self, type_name):
        super().__init__()
        self.type_name = type_name
        self.category = self.STRUCTURE
        self.declarations = []
        self.extensions = []
        self._abi = None
        self._size = None
        self._alignment = None
        self._layout = None

    def set_abi(self, abi):
        """
        Set the ABI object that provides structure layout rules.
        """
        self._abi = abi

    def _update_layout(self):
        """
        Determine the layout of the structure.
        """
        packed = self.is_packed
        member_list = iter(self._members())
        layout = []

        abi = self._abi

        # The layout is (member name, member type, member offset).

        offset = 0
        max_alignment = 1

        for member_name, member_type, member_decl in member_list:
            if member_decl.offset is not None:
                member_pos = int(member_decl.offset)
                if member_pos < offset:
                    raise DSLError("structure {}: "
                                   "Fixed offset of member (@{:d}) is "
                                   "before the end of the previous member "
                                   "(@{:d})".format(self.type_name,
                                                    member_pos, offset),
                                   member_decl.member_name)
                elif member_pos > offset:
                    layout.append(('pad_to_{:s}_'.format(member_name),
                                   StructurePadding(member_pos - offset),
                                   member_pos))
                offset = member_pos
            else:
                member_pos = None
            if not packed:
                pos = abi.layout_struct_member(offset, max_alignment,
                                               member_type.size,
                                               member_type.alignment)
                if pos > offset:
                    if member_pos is not None:
                        raise DSLError("structure {}: "
                                       "Padding needed after fixed offset "
                                       "({:d} bytes)".format(self.type_name,
                                                             pos - offset),
                                       member_decl.member_name)
                    layout.append(('pad_to_{:s}_'.format(member_name),
                                   StructurePadding(pos - offset), pos))
                    offset = pos
            layout.append((member_name, member_type, offset))
            offset += member_type.size
            max_alignment = max(max_alignment, member_type.alignment)

        if offset == 0:
            raise DSLError("Structure has no members", self.type_name)

        # update max_alignment for end padding
        for q in self.qualifiers:
            if q.is_aligned:
                max_alignment = max(max_alignment, q.align_bytes)

        if not packed:
            # Pad the structure at the end
            end = abi.layout_struct_member(offset, max_alignment, None, None)
            if end > offset:
                layout.append(('pad_end_', StructurePadding(end - offset),
                               offset))
                offset = end

        self._size = offset
        self._alignment = max_alignment
        self._layout = tuple(layout)

    def gen_forward_decl(self):
        """
        Generates a forward declaration (if needed) for the type.
        """
        code = []
        code.append("typedef")

        for q in self.qualifiers:
            if q.is_aligned or q.is_packed:
                pass
            elif q.is_atomic or q.is_const:
                code.extend(q.gen_qualifier())
            else:
                raise DSLError("Invalid qualifier for structure", q.name)
        code.append("struct " + self.type_name + ' ' + self.type_name + '_t'
                    ";\n")

        return (code)

    def gen_code(self):
        if self._layout is None:
            self._update_layout()

        code = []
        extra = []

        code.append("struct ")
        for q in self.qualifiers:
            if q.is_aligned or q.is_atomic or q.is_const:
                pass
            elif q.is_packed:
                code.extend(q.gen_qualifier())
            else:
                raise DSLError("Invalid qualifier for structure", q.name)

        code.append(" " + self.type_name + " {\n")

        for q in self.qualifiers:
            if q.is_aligned:
                code.extend(q.gen_qualifier())

        for member_name, member_type, member_offset in self._layout:
            l, r = member_type.gen_expr()
            code.extend(l)
            code.append(member_name)
            code.extend(r)

        code.append("} ")

        code.append(';\n\n')

        return (code, extra)

    @property
    def size(self):
        if self._size is None:
            self._update_layout()
        return self._size

    @property
    def layout_with_padding(self):
        if self._layout is None:
            self._update_layout()
        return self._layout

    @property
    def layout(self):
        return ((n, t, p)
                for n, t, p in self.layout_with_padding
                if not isinstance(t, StructurePadding))

    @property
    def default_alignment(self):
        if self._alignment is None:
            self._update_layout()
        return self._alignment

    def _members(self, prefix=None):
        for d in self.declarations:
            yield from d.get_members(prefix=prefix)
        for e in self.extensions:
            yield from e._members(prefix=prefix)

    @property
    def dependencies(self):
        """
        Return all definitions that this type relies on.

        Note, pointers and other non definitions are not considered
        dependencies as they can be forward declared.
        """
        return itertools.chain(*(t.dependencies
                                 for _, t, _ in self._members()))


class ObjectDefinition(StructureDefinition):
    def __init__(self, type_name):
        super().__init__(type_name)
        self.category = self.OBJECT
        # Object definitions are only generated for objects that are never
        # embedded in any other object.
        # FIXME: this should be explicit in the language
        self.need_export = True

    def gen_code(self):
        if self.need_export:
            return super().gen_code()
        else:
            return ([], [])

    def gen_result_types(self):
        if self.need_export:
            return super().gen_result_types()
        else:
            return ()


class UnionDefinition(IGenCode, ICustomizedDefinition):
    def __init__(self, type_name):
        super().__init__()
        self.type_name = type_name
        self.category = self.UNION
        self.declarations = []
        self.extensions = []
        self._size = None
        self._alignment = None

    def gen_forward_decl(self):
        """
        Generates a forward declaration (if needed) for the type.
        """
        code = []
        code.append("typedef")

        for q in self.qualifiers:
            if q.is_aligned:
                pass
            elif q.is_atomic or q.is_const:
                code.extend(q.gen_qualifier())
            else:
                raise DSLError("Invalid qualifier for union", q.name)
        code.append("union " + self.type_name + ' ' + self.type_name + '_t;\n')

        return code

    def gen_code(self):
        code = []

        code.append('union ')
        for q in self.qualifiers:
            if q.is_aligned or q.is_atomic or q.is_const:
                pass
            else:
                raise DSLError("Invalid qualifier for union", q.name)

        code.append(" " + self.type_name + " {\n")

        align_qualifiers = tuple(q for q in self.qualifiers if q.is_aligned)

        for member_name, member_type, member_decl in self._members():
            if member_decl.offset is not None and int(member_decl.offset) != 0:
                raise DSLError("Union member must have zero offset",
                               member_decl.member_name)
            for q in align_qualifiers:
                code.extend(q.gen_qualifier())
            l, r = member_type.gen_expr()
            code.extend(l)
            code.append(member_name)
            code.extend(r)

        code.append("} ")

        code.append(';\n\n')

        return (code, [])

    @property
    def size(self):
        if self._size is None:
            self._size = max(t.size for _, t, _ in self._members())
        return self._size

    @property
    def default_alignment(self):
        if self._alignment is None:
            q_align = max(
                (q.align_bytes for q in self.qualifiers if q.is_aligned),
                default=1)
            m_align = max(t.alignment for _, t, _ in self._members())
            self._alignment = max(q_align, m_align)
        return self._alignment

    def _members(self, prefix=None):
        for d in self.declarations:
            members = d.get_members(prefix=prefix)
            if len(members) > 1:
                raise DSLError("Unions must not contain flattened objects",
                               d.member_name)
            yield from members
        for e in self.extensions:
            yield from e._members(prefix=prefix)

    @property
    def dependencies(self):
        """
        Return all definitions that this type relies on.

        Note, pointers and other non definitions are not considered
        dependencies as they can be forward declared.
        """
        return itertools.chain(*(t.dependencies
                                 for _, t, _ in self._members()))


class EnumerationConstant(object):
    __slots__ = ['name', 'value', 'prefix']

    def __init__(self, name, value=None):
        self.name = name
        self.value = value
        self.prefix = None


enum_const = namedtuple('enum_const', ['name', 'value', 'prefix'])


class EnumerationDefinition(IGenCode, ICustomizedDefinition):
    def __init__(self, type_name):
        super().__init__()
        self.type_name = type_name
        self.category = self.ENUMERATION
        self._enumerators = []
        self.prefix = None
        self.capitalized = True
        self._explicit = False
        self._signed = None
        self._size = None
        self._bitsize = None
        self._alignment = None
        self._min_value = None
        self._max_value = None
        self._updated = False

    def __getstate__(self):
        """
        Temporary workaround to ensure types are updated before pickling.  Auto
        update should be removed entirely.
        """
        if not self._updated:
            self._autoupdate()
        return self.__dict__

    def set_abi(self, abi):
        """
        Set the ABI object that provides structure layout rules.
        """
        self._abi = abi

    def set_explicit(self):
        """
        Set the enumeration as being explicit, no value allocation.
        """
        self._explicit = True

    def add_enumerator(self, e):
        """
        Add an enumerator to the type.
        """
        # If this is the first enumerator, its default value is zero
        if e.value is None and len(self._enumerators) == 0:
            if self._explicit:
                raise DSLError("auto allocated enumerator in explicit "
                               "enumeration", e.name)
            e.value = 0

        self._enumerators.append(e)

    def _autoupdate(self):
        """
        Update internal data, prepare to generate code
        """
        def _check_enumerator(e):
            if e.value in used_values:
                raise DSLError("each enumerator needs to have a unique value",
                               e.name)
            used_values.add(e.value)

        # Ensure constant values are resolved and not duplicates
        used_values = set()
        for e in self._enumerators:
            if e.value is not None:
                e.value = int(e.value)
                _check_enumerator(e)

        # Auto-allocation of remaining values
        last_val = None
        for e in self._enumerators:
            if e.value is None and self._explicit:
                raise DSLError("auto allocated enumerator in explicit "
                               "enumeration", e.name)
            if e.value is None:
                assert last_val is not None
                e.value = last_val + 1
                _check_enumerator(e)
            last_val = e.value

        if not self._enumerators:
            raise DSLError('Empty enumeration', self.type_name)
        e_min = min(used_values)
        e_max = max(used_values)
        self._size, self._alignment, self._signed = \
            self._abi.get_enum_properties(e_min, e_max)
        self._bitsize = max(e_min.bit_length(), e_max.bit_length())
        self._min_value = e_min
        self._max_value = e_max

        if self.prefix is None:
            self.prefix = self.type_name + '_'
            if self.capitalized:
                self.prefix = self.prefix.upper()

        # Todo: support suppressing prefix for some enumerators
        for e in self._enumerators:
            if e.prefix is None:
                e.prefix = self.prefix

        # Finalize
        enumerators = [enum_const(e.name, e.value, e.prefix) for e in
                       self._enumerators]
        self._enumerators = enumerators
        self._updated = True

    @property_autoupdate
    def enumerators(self):
        return self._enumerators

    @property_autoupdate
    def size(self):
        return self._size

    @property_autoupdate
    def bitsize(self):
        return self._bitsize

    @property_autoupdate
    def minimum_value(self):
        return self._min_value

    @property_autoupdate
    def maximum_value(self):
        return self._max_value

    @property_autoupdate
    def default_alignment(self):
        return self._alignment

    @property_autoupdate
    def is_signed(self):
        return self._signed

    def get_enum_name(self, e):
        if self.capitalized:
            return e.name.upper()
        else:
            return e.name

    def gen_code(self):
        if not self._updated:
            self._autoupdate()

        code = []
        extra = []

        # generate code now
        code = ['typedef', 'enum', self.type_name, '{\n']

        sorted_enumerators = sorted(self._enumerators, key=lambda x: x.value)

        code.append(',\n'.join('  ' + e.prefix + self.get_enum_name(e) +
                               ' = ' + str(e.value)
                               for e in sorted_enumerators))

        code.append('\n}')

        code.append(self.type_name + '_t')

        code.append(';\n\n')

        suffix = '' if self.is_signed else 'U'
        code.append('#define {:s}__MAX ({:s})({:d}{:s})\n'.format(
            self.type_name.upper(), self.type_name + '_t', self.maximum_value,
            suffix))
        code.append('#define {:s}__MIN ({:s})({:d}{:s})\n'.format(
            self.type_name.upper(), self.type_name + '_t', self.minimum_value,
            suffix))
        code.append('\n')

        return (code, extra)

    @property
    def dependencies(self):
        """
        Return all definitions that this type relies on.

        Note, pointers and other non definitions are not considered
        dependencies as they can be forward declared.
        """
        return ()


class IDeclarationDefinition(IGenCode, ICustomizedDefinition, IDeclaration):
    def __init__(self, type_name):
        super().__init__()
        self.type_name = type_name

    @property
    def size(self):
        return self.compound_type.size

    @property
    def bitsize(self):
        return self.compound_type.bitsize

    @property
    def default_alignment(self):
        return self.compound_type.alignment

    @property
    def is_signed(self):
        return self.compound_type.is_signed

    @property
    def dependencies(self):
        """
        Return all definitions that this type relies on.

        Note, pointers and other non definitions are not considered
        dependencies as they can be forward declared.
        """
        if self.compound_type is None:
            return ()
        return self.compound_type.dependencies


class AlternativeDefinition(IDeclarationDefinition):
    def __init__(self, type_name):
        super().__init__(type_name)
        self.category = self.ALTERNATIVE

    def gen_code(self):
        code = []
        extra = []

        # generate code now
        l, r = self.compound_type.gen_expr()
        code = ["typedef"] + l + [self.type_name] + r + ['\n']

        return (code, extra)

    def gen_result_types(self):
        """
        Generate result and pointer-result wrapper structures for the type.
        """
        if not self.type_name.endswith('_t'):
            return super().gen_result_types()
        r = "HYPTYPES_DECLARE_RESULT({:s})\n".format(self.type_name[:-2])
        rp = "HYPTYPES_DECLARE_RESULT_PTR({:s})\n".format(self.type_name[:-2])
        if self.compound_type.is_array:
            return (rp,)
        return (r, rp)


class ConstantDefinition(IDeclarationDefinition):
    def __init__(self, type_name, value):
        super().__init__(type_name)
        self.value = value
        self.category = self.CONSTANT

    def gen_code(self):
        code = []
        extra = []

        # generate code now
        val = int(self.value)
        if self.compound_type is not None:
            # FIXME: find better solution
            l, _ = self.compound_type.gen_type()
            cast = '({:s})'.format(' '.join(l))
            suffix = '' if self.compound_type.is_signed else 'U'
            if val < 0 and not self.compound_type.is_signed:
                val &= ((1 << (self.compound_type.size * 8)) - 1)
        else:
            cast = ''
            suffix = ''
        code.append("#define {:s} {:s}{:d}{:s}\n".format(
            self.type_name, cast, val, suffix))

        return (code, extra)

    def gen_result_types(self):
        """
        Generate result and pointer-result wrapper structures for the type.
        """
        return ()


class BitFieldDefinition(IGenCode, ICustomizedDefinition):
    TYPE_TEMPLATE = "templates/bitfield-type.tmpl"

    def __init__(self, type_name):
        super().__init__()
        self.type_name = type_name
        self.length = -1
        self.unit_type = "uint64_t"
        self.unit_size = -1
        self.declarations = []
        self.extensions = []
        self.category = self.BITFIELD
        self._ranges = None
        self.const = False
        self._signed = False

    def update_unit_info(self):
        if self.length <= 8:
            self.unit_size = 8
            self.unit_type = "uint8_t"
        elif self.length <= 16:
            self.unit_size = 16
            self.unit_type = "uint16_t"
        elif self.length <= 32:
            self.unit_size = 32
            self.unit_type = "uint32_t"
        elif self.length <= 64:
            self.unit_size = 64
            self.unit_type = "uint64_t"
        else:
            self.unit_size = 64
            self.unit_type = "uint64_t"

    @property
    def size(self):
        return math.ceil(self.length / self.unit_size) * self.unit_size // 8

    @property
    def ranges(self):
        if self._ranges is None:
            self._update_layout()
        return self._ranges

    @property
    def bitsize(self):
        return max(r[1] + 1 for r in self.ranges.alloc_list)

    @property
    def default_alignment(self):
        return self.unit_size // 8

    @property
    def is_signed(self):
        return self._signed

    @property
    def _all_declarations(self):
        for d in self.declarations:
            yield d
        for e in self.extensions:
            for d in e.declarations:
                yield d

    @property
    def fields(self):
        for d in self._all_declarations:
            if d.compound_type is not None:
                yield d

    @property
    def all_fields_boolean(self):
        return all(m.compound_type.bitsize == 1 for m in self.fields)

    def _gen_definition_code(self):
        """
        Return type definition code if this type needs it.
        """
        if self.ranges.range_auto:
            raise Exception("unhandled auto ranges")
            self.length = self.ranges.free_list[0][1] - 1
            self.update_unit_info()

        # generate type definition by template
        ns = {
            "type_name": self.type_name,
            "unit_type": self.unit_type,
            "unit_cnt": self.unit_count,
            "init_values": self.init_values,
            "compare_masks": self.compare_masks,
            "all_fields_boolean": self.all_fields_boolean,
        }
        fn = os.path.join(__loc__, self.TYPE_TEMPLATE)
        t = Template(file=open(fn, 'r', encoding='utf-8'), searchList=ns)
        return str(t)

    @property
    def unit_count(self):
        return math.ceil(self.length / self.unit_size)

    @property
    def init_values(self):
        init_value = 0
        for d in self._all_declarations:
            if d.default is None:
                continue
            val = int(d.default.value)
            if d.field_length is None and val != 0:
                raise DSLError(
                    "bitfield others must not have a nonzero default",
                    d.member_name)
            if val == 0:
                continue
            if val.bit_length() > d.field_length:
                raise DSLError("Bitfield default value does not fit in field",
                               d.member_name)
            for field_map in d.field_maps:
                field_mask = (1 << field_map.length) - 1
                # First mask out any reserved bits that have been replaced by a
                # field in an extension.
                init_value &= ~(field_mask << field_map.mapped_bit)
                init_value |= (((val >> field_map.field_bit) & field_mask) <<
                               field_map.mapped_bit)
        unit_mask = (1 << self.unit_size) - 1
        return tuple((init_value >> (i * self.unit_size)) & unit_mask
                     for i in range(self.unit_count))

    @property
    def compare_masks(self):
        compare_mask = 0
        for d in self.fields:
            if d.is_writeonly:
                continue
            for field_map in d.field_maps:
                field_mask = (1 << field_map.length) - 1
                compare_mask |= field_mask << field_map.mapped_bit
        unit_mask = (1 << self.unit_size) - 1
        return tuple((compare_mask >> (i * self.unit_size)) & unit_mask
                     for i in range(self.unit_count))

    def gen_code(self):
        code = []
        extra = []

        code.append(self._gen_definition_code())

        # generate getters and setters for all declarations
        for d in self._all_declarations:
            if d.compound_type is None:
                continue
            if d.bitfield_specifier is None:
                raise DSLError("each declaration needs to specify logical" +
                               " physical bit map", d.member_name)
            else:
                c, e = d.gen_code()
                code += c
                extra += e

        return (code, extra)

    def update(self):
        super().update()
        if self._ranges is None:
            self._update_layout()

    def _update_layout(self):
        """
        Determine the layout of the bitfield.
        """

        for e in self.extensions:
            for name in e.delete_items:
                found = False
                for i, d in enumerate(self.declarations):
                    if str(d.member_name) == name:
                        del(self.declarations[i])
                        found = True
                        break
                if not found:
                    raise DSLError("can't delete unknown member", name)

        self._ranges = BitFieldRangeCollector(self.length)
        for d in self._all_declarations:
            d.update_ranges(self._ranges)

        for d in self._all_declarations:
            if d.is_ignore:
                continue
            # if bitfield is constant, update all members
            if self.const:
                d.set_const()

            # share definition information to declarations
            d.bf_type_name = self.type_name
            d.unit_type = self.unit_type
            d.unit_size = self.unit_size
            d.ranges = self._ranges

            d.update()

    @property
    def dependencies(self):
        """
        Return all definitions that this type relies on.

        Note, pointers and other non definitions are not considered
        dependencies as they can be forward declared.
        """
        for m in self.fields:
            yield from m.compound_type.dependencies


class StructureExtension(IExtension):
    def __init__(self, type_name):
        super().__init__()
        self.type_name = type_name
        self.indicator = type_name
        self.module_name = None
        self.category = self.STRUCTURE

    def link(self, definition):
        self.prefix = self.module_name
        definition.extensions.append(self)

    def _members(self, prefix=None):
        if prefix is not None and self.prefix is not None:
            p = prefix + "_" + self.prefix
        elif prefix is not None:
            p = prefix
        else:
            p = self.prefix

        return itertools.chain(*(d.get_members(prefix=p)
                                 for d in self.declarations))


class ObjectExtension(StructureExtension):
    def __init__(self, type_name):
        super().__init__(type_name)
        self.category = self.OBJECT


class UnionExtension(IExtension):
    def __init__(self, type_name):
        super().__init__()
        self.type_name = type_name
        self.indicator = type_name
        self.module_name = None
        self.definition = NotImplemented
        self.category = self.UNION

    def link(self, definition):
        self.prefix = self.module_name
        definition.extensions.append(self)

    def _members(self, prefix=None):
        if prefix is not None and self.prefix is not None:
            p = prefix + "_" + self.prefix
        elif prefix is not None:
            p = prefix
        else:
            p = self.prefix

        for d in itertools.chain(self.declarations):
            members = d.get_members(prefix=p)
            if len(members) > 1:
                raise DSLError("Unions must not contain flattened objects",
                               d.member_name)
            yield from members


class EnumerationExtension(IExtension):
    def __init__(self, type_name):
        super().__init__()
        self.type_name = type_name
        self.indicator = type_name
        self.module_name = None
        self.category = self.ENUMERATION
        self._enumerators = []

    def add_enumerator(self, e):
        """
        Add an enumerator to the extension.
        """
        self._enumerators.append(e)

    def link(self, definition):
        for e in self._enumerators:
            definition.add_enumerator(e)


class BitFieldExtension(IExtension, IGenCode, IUpdate):
    def __init__(self, type_name):
        super().__init__()
        self.type_name = type_name
        self.indicator = type_name
        self.module_name = None
        self.category = self.BITFIELD
        self.delete_items = set()

    def link(self, definition):
        # FIXME check if declarations in extension overlap the original bit
        # fields

        # there's only one level for bitfield extension
        # change the name of declaration as ModuleName__member_name
        if self.module_name is not None:
            self.prefix = self.module_name

        # update definition's declarations list
        definition.extensions.append(self)

    def update(self):
        super().update()

        # Set the module prefix on all our declarations
        if self.prefix is not None:
            for d in self.declarations:
                d.prefix = self.prefix

    def add_delete_member(self, name):
        if name in self.delete_items:
            raise DSLError("delete item: {:s} duplicated".format(name), name)
        self.delete_items.add(name)

    def gen_code(self):
        code = []
        extra = []

        for d in self.declarations:
            c, e = d.gen_code()
            code += c
            extra += e

        return (code, extra)


class Qualifier:
    def __init__(self, name):
        self.name = name

    @property
    def is_const(self):
        return self.name == 'const'

    @property
    def is_writeonly(self):
        return self.name == 'writeonly'

    @property
    def is_restrict(self):
        return self.name == 'restrict'

    @property
    def is_atomic(self):
        return False

    @property
    def is_aligned(self):
        return False

    @property
    def is_packed(self):
        return False

    @property
    def is_contained(self):
        return False

    def gen_qualifier(self):
        return [self.name]

    def __eq__(self, other):
        return (str(self.name) == str(other.name))

    def __hash__(self):
        return hash(str(self.name))


class AlignedQualifier(Qualifier):
    def __init__(self, name, align_bytes):
        super().__init__(name)
        self._align_bytes_expr = align_bytes
        self._align_bytes = None

    @property
    def align_bytes(self):
        if self._align_bytes is None:
            align_bytes = int(self._align_bytes_expr)
            if align_bytes <= 0:
                raise DSLError("Alignment {:d} is not positive"
                               .format(align_bytes), self.name)
            if align_bytes != 1 << (align_bytes - 1).bit_length():
                raise DSLError("Alignment {:d} is not a power of two"
                               .format(align_bytes), self.name)
            self._align_bytes = align_bytes
        return self._align_bytes

    def gen_qualifier(self):
        return ["alignas({:d})".format(self.align_bytes)]

    @property
    def is_aligned(self):
        return self.align_bytes

    def __eq__(self, other):
        return (id(self) == id(other))

    def __hash__(self):
        return hash(id(self))


class AtomicQualifier(Qualifier):
    def gen_qualifier(self):
        return ["_Atomic"]

    @property
    def is_atomic(self):
        return True


class PackedQualifier(Qualifier):
    def gen_qualifier(self):
        return ["__attribute__((packed))"]

    @property
    def is_packed(self):
        return True


class ContainedQualifier(Qualifier):
    def gen_qualifier(self):
        return [""]

    @property
    def is_contained(self):
        return True


class BitFieldRangeCollector:
    """
    BitFieldRangeCollector manages the range defined by length [0, length).
    It addresses the range manage issue from Bit Field declaration.
    """

    def __init__(self, length):
        if length == -1:
            self.range_auto = True
        else:
            self.range_auto = False
        self.free_list = [(0, length - 1 if length != -1 else 0)]
        self.alloc_list = []
        self.reserved_list = []
        self.origLength = length

    def insert_range(self, bit_range, declaration):
        """
        Check if the [start, end] is inside existing range
        definition.
        If it's inside the range, remove specified range, and return True.
        Else return False.
        """
        start = bit_range[0]
        end = bit_range[0] + bit_range[1] - 1
        if start < 0 or end < 0 or start > end:
            return False

        # NOTE: it's OK only if not continue loop after find the target
        for i, (s, e) in enumerate(self.free_list):
            if s <= start and e >= end:
                if declaration.is_ignore:
                    self.reserved_list.append((start, end, declaration))
                    # FIXME: check for reserved overlaps
                    return True

                del self.free_list[i]

                if s < start:
                    self.free_list.insert(i, (s, start - 1))
                    i += 1

                if e > end:
                    self.free_list.insert(i, (end + 1, e))

                self.alloc_list.append((start, end, declaration))

                return True

        return False

    def alloc_range(self, length, declaration):
        """
        Assumption, it's only used by auto logical physical mapping. This use
        case only happens for software bit field definition. It seldom to
        define scattered bit field members. Contiguously available space can
        be find easily.
        Also the typical bit field length is less than 64 bits (maybe 128
        bits), no need to handle alignment right now.

        Return the lowest fragment which satisfy the length requirement.
        Also mark it as used
        """
# FIXME: - option to keep fragment in one word (for bitfields) ?
        if self.range_auto:
            self.free_list[0] = (self.free_list[0][0],
                                 self.free_list[0][1] + length - 1)
        ret = None
        for (s, e) in self.free_list:
            sz = e - s + 1
            if sz >= length:
                ret = (s, length)
                self.insert_range(ret, declaration)
                break

        return ret

    def is_empty(self):
        return len(self.free_list) == 0

    def __repr__(self):
        msg = []
        for s, e in self.free_list:
            msg.append("[" + str(e) + ":" + str(s) + "]")
        return 'Range len(%d), ranges available: %s' % \
            (self.origLength, ','.join(msg))


class TransformTypes(Transformer):
    """
    Bottom up traversal helper. It overrides Transformer to do the
    traversal. Use CommonTree as
    the default tree node.
    """

    def __init__(self, program):
        super().__init__()
        self.program = program

    def __default__(self, children, meta, data):
        "Default operation on tree (for override)"
        import ast_nodes
        return ast_nodes.CommonListener(self.program, children, meta, data)

    def node_handler(self, name):
        import ast_nodes
        x = getattr(ast_nodes, name)
        if not inspect.isclass(x):
            raise AttributeError
        if not issubclass(x, ast_nodes.CommonTree):
            raise AttributeError
        if type(x) in [ast_nodes.CommonTree]:
            raise AttributeError
        return x

    def _call_userfunc(self, tree, new_children=None):
        # Assumes tree is already transformed
        children = new_children if new_children is not None else tree.children
        try:
            f = self.node_handler(tree.data)
        except AttributeError:
            ret = self.__default__(children, tree.meta, tree.data)
        else:
            ret = f(self.program, children, tree.meta)

        return ret

    def _transform_tree(self, tree):

        children = list(self._transform_children(tree.children))

        ret = self._call_userfunc(tree, children)

        return ret

    def _transform_children(self, children):
        import ast_nodes
        for c in children:
            try:
                if isinstance(c, Tree):
                    yield self._transform_tree(c)
                elif isinstance(c, Token):
                    yield ast_nodes.TToken(str(c), c, self.program)
                else:
                    yield c
            except Discard:
                pass
