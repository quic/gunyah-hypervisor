# © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
#
# 2019 Cog Systems Pty Ltd.
#
# SPDX-License-Identifier: BSD-3-Clause

from lark import Tree, Token
import itertools
from ir import (
    TopLevel, PrimitiveType, BitFieldDeclaration, BitFieldType, StructureType,
    EnumerationType, AlternativeType, BitFieldSpecifier, DirectType,
    PointerType, PrimitiveDeclaration, ArrayType, ConstantDefinition,
    AlternativeDefinition, BitFieldDefinition, StructureDefinition,
    EnumerationDefinition, EnumerationConstant, EnumerationExtension,
    ObjectType, ObjectDeclaration, ObjectDefinition, BitFieldExtension,
    ObjectExtension, Qualifier, AlignedQualifier, AtomicQualifier,
    PackedQualifier, ConstantExpression, ConstantReference, UnaryOperation,
    SizeofOperation, AlignofOperation, BinaryOperation, ConditionalOperation,
    UnionType, UnionDefinition, UnionExtension, StructureExtension,
    MinofOperation, MaxofOperation, ContainedQualifier,
)
from exceptions import DSLError

"""
The classes in the module represent nodes in the AST.
They are used only to parse the input.

All classes that inherit from CommonTree in this file  will be automatically
imported into the TransformTypes transformer (excluding CommonTree itself).
"""


def toint(text):
    """
    Convert value strings to integers.

    Supports Python styles for decimal, hex and binary (but not octal). Also
    supports (and ignores) a C-style U suffix.
    """
    text = text.rstrip('uU')
    if len(text) > 1 and text[0] == '0' and text[1] not in 'xXbB':
        raise DSLError('Unknown base for value {:s}'.format(text), text)

    return int(text, base=0)


class CommonTree(Tree):
    """
    Common class for all AST nodes
    """

    def __init__(self, program, children, meta, data=None):
        if data is None:
            data = self.__class__.__name__
        super().__init__(data, children, meta)
        self.program = program

    def pass_up(self):
        pass

    @property
    def num_children(self):
        return len(self.children)


class TToken(str):
    __slots__ = ['program', 'line', 'column', 'pos_in_stream']

    def __new__(cls, val, token=None, program=None, line=None, column=None,
                pos_in_stream=None):
        self = super(TToken, cls).__new__(cls, val)
        if token:
            line = token.line
            column = token.column
            pos_in_stream = token.pos_in_stream

        self.program = program
        self.line = line
        self.column = column
        self.pos_in_stream = pos_in_stream
        return self

    def __reduce__(self):
        return (TToken, (str(self), None, self.program, self.line, self.column,
                         self.pos_in_stream))


class Action:
    """
    A class helps rules to register function to provide input for parent node.
    Parameter:
    fn: function to call if parent decide to take this action.
        The signature of this actionis "def f(object)". This action can handle
    the object as it wants.
    name: the rule name who provides this action
    passive: True indicate it needs to be specifically called
    trace: the tree node who provides this action. Just for debug
    """

    def __init__(self, fn, name, passive=False, trace=None):
        self.name = name
        self.trace = trace
        self.fn = fn
        self.passive = passive

    def take(self, obj):
        """
        Take this action, and change the specified obj
        """
        if self.trace:
            print("take ", self)
        return self.fn(obj)

    def __repr__(self):
        more = ""
        if self.trace:
            more = "<" + str(self.trace) + ">"
        return "action %s from %s%s" % (self.fn.__name__, self.name, more)

    def match(self, rule_name):
        return self.name == rule_name


class ActionList:
    """
    The helper to manipulate the actions.
    """

    def __init__(self, actions=[]):
        self.actions = actions.copy()

    def __iter__(self):
        return itertools.chain(self.actions)

    def __iadd__(self, x):
        if isinstance(x, ActionList):
            self.actions += x.actions
            return self
        else:
            raise Exception("only allowed to iadd Action List" + str(type(x)))

    def __repr__(self):
        ret = []
        for a in self.actions:
            ret.append("    " + str(a))
        return "Action list: \n" + '\n'.join(ret)

    def append_actions(self, action_list):
        self.actions += action_list

    def take_all(self, obj, accept_list=[], deny_list=[], remove=True):
        if set(accept_list) & set(deny_list):
            raise Exception("cannot accept/deny same name at the same time: " +
                            ', '.join(set(accept_list) & set(deny_list)))

        for a in list(self.actions):
            if len(accept_list) > 0 and a.name in accept_list:
                a.take(obj)
            elif len(deny_list) > 0 and a.name not in deny_list:
                a.take(obj)
            else:
                continue

            if remove:
                self.actions.remove(a)

    def take(self, obj, name, remove=True, single=True):
        """
        Take actions, can get the return value
        parameters:
        obj: the object who receive the action result
        name: specify the action name to take
        remove: indicate if need to remove the action after take it
        single: indicate if need to take all actions who has the same name.
            If so, just return the last action's return value.

        """
        ret = None
        for a in list(self.actions):
            if a.name == name:
                if remove:
                    self.actions.remove(a)

                if single:
                    return a.take(obj)

                ret = a.take(obj)

        return ret

    def remove_all(self, remove_list):
        self.actions = [a for a in self.actions if a.name not in remove_list]

    def has(self, name):
        for a in self.actions:
            if name == a.name:
                return True
        return False

    def remains(self):
        return len(self.actions) != 0


class CommonListener(CommonTree):
    """
    Common class for all list nodes need to sync.

    Order: Since it's a bottom up node, all children visitor (for read/write)
    is ahead of parent, and left is ahead of right.
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self.actions = []
        self.action_list = ActionList()

        # get all actions from children
        for c in self.children:
            if isinstance(c, CommonListener):
                self.action_list += c.pass_up()

        self.init_interfaces()

    def init_interfaces(self):
        """
        In this function, node can:
        * initialize all attributes which will be set by other nodes.
        * set actions for parent nodes
        * choose some actions to take
        """
        pass

    def pass_up(self):
        """
        Setup accepts of this nodes, parents will collect this node's accepts,
        and call them properly.
        By default, it just return remaining accepts
        """
        self.action_list.append_actions(self.actions)
        return self.action_list


class start(CommonListener):
    """
    The root node of the the AST
    """

    def init_interfaces(self):
        self.intermediate_tree = TopLevel()
        al = [
            "bitfield_extension",
            "bitfield_definition",
            "object_definition",
            "object_extension",
            "structure_definition",
            "structure_extension",
            "union_definition",
            "union_extension",
            "enumeration_definition",
            "enumeration_extension",
            "alternative_definition",
            "constant_definition",
            "declaration",
            "add_abi_ref",
            "add_constant_ref",
            "add_type_ref",
        ]
        self.action_list.take_all(self.intermediate_tree, al)

        if self.action_list.remains():
            print("Warning: untaken actions remain. ", self.action_list)

    def get_intermediate_tree(self):
        return self.intermediate_tree


"""
Add a pass_up method to tokens so we can treat them the same way as nodes
"""
Token.pass_up = lambda self: self


class constant_value(CommonListener):
    def init_interfaces(self):
        self.value = toint(self.children[0])


class constant_reference(CommonListener):
    def init_interfaces(self):
        self.value = ConstantReference(self.children[0])
        self.actions.append(Action(self.set_symbol_ref, 'add_constant_ref'))

    def set_symbol_ref(self, obj):
        obj.constant_refs.append(self.value)


class constant_expression(CommonListener):
    def init_interfaces(self):
        self.value = ConstantExpression(self.children[0].value)


bracketed_constant_expression = constant_expression


class unary_operation(CommonListener):
    def init_interfaces(self):
        self.value = UnaryOperation(self.children[0], self.children[1].value)


class IConstantFromTypeOperation(CommonListener):
    def init_interfaces(self):
        self.type_ref = None
        al = ["direct_type", "array", "pointer", "alias",
              "primitive_type", "bitfield_type", "structure_type",
              "union_type", "object_type", "enumeration_type",
              "alternative_type"]
        self.action_list.take_all(self, al)

        rl = [
            "pointer_has_pointer",
            "object_type_set_complex",
            "object_type_has_object",
            "object_type_create_declaration",
        ]

        self.action_list.remove_all(rl)
        self.actions.append(Action(self.set_type_ref, 'add_type_ref'))

    def set_type_ref(self, obj):
        if self.type_ref is not None:
            obj.type_refs.append(self.type_ref)


class sizeof_operation(IConstantFromTypeOperation):
    def init_interfaces(self):
        super().init_interfaces()
        self.value = SizeofOperation(self.compound_type)


class alignof_operation(IConstantFromTypeOperation):
    def init_interfaces(self):
        super().init_interfaces()
        self.value = AlignofOperation(self.compound_type)


class minof_operation(IConstantFromTypeOperation):
    def init_interfaces(self):
        super().init_interfaces()
        self.value = MinofOperation(self.compound_type)


class maxof_operation(IConstantFromTypeOperation):
    def init_interfaces(self):
        super().init_interfaces()
        self.value = MaxofOperation(self.compound_type)


class IBinaryOperation(CommonListener):
    def init_interfaces(self):
        self.value = BinaryOperation(self.children[1], self.children[0].value,
                                     self.children[2].value)


class mult_operation(IBinaryOperation):
    pass


class add_operation(IBinaryOperation):
    pass


class shift_operation(IBinaryOperation):
    pass


class relational_operation(IBinaryOperation):
    pass


class equality_operation(IBinaryOperation):
    pass


class IFixedBinaryOperation(CommonListener):
    def init_interfaces(self):
        self.value = BinaryOperation(self.operator, self.children[0].value,
                                     self.children[1].value)


class bitwise_and_operation(IFixedBinaryOperation):
    operator = "&"


class bitwise_xor_operation(IFixedBinaryOperation):
    operator = "^"


class bitwise_or_operation(IFixedBinaryOperation):
    operator = "|"


class logical_and_operation(IFixedBinaryOperation):
    operator = "&&"


class logical_or_operation(IFixedBinaryOperation):
    operator = "||"


class conditional_operation(CommonListener):
    def init_interfaces(self):
        self.value = ConditionalOperation(self.children[0].value,
                                          self.children[1].value,
                                          self.children[2].value)


class IABISpecific:
    """
    Mixin that triggers a call to set_abi().

    This can be used either on a declaration or on a customised type.
    """

    def init_interfaces(self):
        super().init_interfaces()
        self.actions.append(Action(self.set_abi, 'add_abi_ref'))

    def set_abi(self, obj):
        if hasattr(self, 'definition'):
            obj.abi_refs.add(self.definition)
        elif hasattr(self, 'compound_type'):
            obj.abi_refs.add(self.compound_type)
        else:
            raise DSLError("Cannot set ABI", self)


class primitive_type(IABISpecific, CommonListener):
    def init_interfaces(self):
        self.type_name = self.children[0]
        self.compound_type = PrimitiveType(self.type_name)
        self.actions = [Action(self.set_type, "primitive_type", self)]
        super().init_interfaces()

    def set_type(self, declaration):
        declaration.compound_type = self.compound_type


class bitfield_type(CommonListener):
    def init_interfaces(self):
        self.type_name = self.children[0]
        self.compound_type = BitFieldType(self.type_name)
        self.actions = [Action(self.set_type, "bitfield_type")]

    def set_type(self, declaration):
        d = declaration
        d.compound_type = self.compound_type
        d.type_ref = d.compound_type
        d.is_customized_type = True


class structure_type(CommonListener):
    def init_interfaces(self):
        self.type_name = self.children[0]
        self.compound_type = StructureType(self.type_name)
        self.actions = [Action(self.set_type, "structure_type")]

    def set_type(self, declaration):
        d = declaration
        d.compound_type = self.compound_type
        d.type_ref = self.compound_type
        d.is_customized_type = True


class union_type(CommonListener):
    def init_interfaces(self):
        self.type_name = self.children[0]
        self.compound_type = UnionType(self.type_name)
        self.actions = [Action(self.set_type, "union_type")]

        if self.action_list.has("object_type_has_object"):
            raise DSLError("cannot declare an object type member in union",
                           self.declaration.member_name)

    def set_type(self, declaration):
        d = declaration
        d.compound_type = self.compound_type
        d.type_ref = self.compound_type
        d.is_customized_type = True


class enumeration_type(CommonListener):
    def init_interfaces(self):
        self.type_name = self.children[0]
        self.compound_type = EnumerationType(self.type_name)
        self.actions = [Action(self.set_type, "enumeration_type")]

    def set_type(self, declaration):
        d = declaration
        d.compound_type = self.compound_type
        d.type_ref = d.compound_type
        d.is_customized_type = True


class alternative_type(CommonListener):
    def init_interfaces(self):
        self.type_name = self.children[0]
        self.compound_type = AlternativeType(self.type_name)
        self.actions = [Action(self.set_type, "alternative_type")]

    def set_type(self, declaration):
        d = declaration
        d.compound_type = self.compound_type
        d.type_ref = self.compound_type
        d.is_customized_type = True


class direct_type(CommonListener):
    def init_interfaces(self):
        self.compound_type = DirectType()
        self.action_list.take(self.compound_type, "qualifier_list")
        self.actions = [Action(self.set_type, "direct_type")]

    def set_type(self, declaration):
        self.compound_type.set_basic_type(declaration.compound_type)
        declaration.compound_type = self.compound_type


class qualifier_list(CommonListener):
    def init_interfaces(self):
        self.qualifiers = set()
        al = [
            "basic_qualifier",
            "atomic_qualifier",
            "packed_qualifier",
            "aligned_qualifier",
            "contained_qualifier"
        ]
        self.action_list.take_all(self, al)
        self.actions = [Action(self.set_qualifiers, "qualifier_list")]

    def set_qualifiers(self, obj):
        obj.qualifiers = self.qualifiers


class basic_qualifier(CommonListener):
    def init_interfaces(self):
        self.qualifier = Qualifier(self.children[0])
        self.actions = [Action(self.add_qualifier, "basic_qualifier")]

    def add_qualifier(self, obj):
        obj.qualifiers.add(self.qualifier)


class atomic_qualifier(CommonListener):
    def init_interfaces(self):
        self.qualifier = AtomicQualifier(self)
        self.actions = [Action(self.add_qualifier, "atomic_qualifier")]

    def add_qualifier(self, obj):
        obj.qualifiers.add(self.qualifier)


class packed_qualifier(CommonListener):
    def init_interfaces(self):
        self.qualifier = PackedQualifier(self)
        self.actions = [Action(self.add_qualifier, "packed_qualifier")]

    def add_qualifier(self, obj):
        obj.qualifiers.add(self.qualifier)


class aligned_qualifier(CommonListener):
    def init_interfaces(self):
        self.action_list.take(self, 'constant_expression')
        self.qualifier = AlignedQualifier(self, self.children[0].value)
        self.actions = [Action(self.add_qualifier, "aligned_qualifier")]

    def add_qualifier(self, obj):
        obj.qualifiers.add(self.qualifier)


class contained_qualifier(CommonListener):
    def init_interfaces(self):
        self.qualifier = ContainedQualifier(self)
        self.actions = [Action(self.add_qualifier, "contained_qualifier")]

    def add_qualifier(self, obj):
        obj.qualifiers.add(self.qualifier)


class array_size(CommonListener):
    def init_interfaces(self):
        self.value = self.children[0].value
        self.actions = [
            Action(self.set_size, "array_size"),
        ]

    def set_size(self, obj):
        obj.length = self.value


class array(CommonListener):
    def init_interfaces(self):
        self.length = NotImplemented
        self.compound_type = ArrayType(self)

        al = ["array_size"]
        self.action_list.take_all(self, al)
        self.compound_type.length = self.length
        al = ["object_type_set_complex"]
        self.action_list.take_all(self.compound_type, accept_list=al)
        self.actions = [Action(self.set_type, "array")]

    def set_type(self, declaration):
        a = self.compound_type
        a.base_type = declaration.compound_type
        declaration.compound_type = a
        declaration.complex_type = True


class pointer(IABISpecific, CommonListener):
    def init_interfaces(self):
        self.compound_type = PointerType(self)
        al = [
            "qualifier_list",
            "pointer_has_pointer",
            "object_type_set_complex"]
        self.action_list.take_all(self.compound_type, accept_list=al)

        self.actions = [
            Action(self.mark_has_pointer, "pointer_has_pointer"),
            Action(self.set_type, "pointer")
        ]

        # Pointers to objects hide an object
        rl = ["object_type_has_object"]
        self.action_list.remove_all(rl)

        super().init_interfaces()

    def set_type(self, declaration):
        self.compound_type.base_type = declaration.compound_type
        declaration.compound_type = self.compound_type
        declaration.complex_type = True

    def mark_has_pointer(self, pointer):
        pointer.has_pointer = True


class declaration(CommonListener):
    def init_interfaces(self):
        # special case to handle object type declaration
        d = self.action_list.take(self, "object_type_create_declaration")
        if d is None:
            self.declaration = PrimitiveDeclaration()
        else:
            self.declaration = d
            self.action_list.take(self.declaration, 'object_noprefix')

        self.declaration.member_name = self.children[0]
        al = ["direct_type", "array", "pointer", "alias", "primitive_type",
              "bitfield_type", "structure_type", "union_type", "object_type",
              "enumeration_type", "alternative_type", "declaration_offset"]
        self.action_list.take_all(self.declaration, al)

        rl = ["pointer_has_pointer", "object_type_set_complex"]
        self.action_list.remove_all(rl)

        self.actions = [Action(self.set_declaration, "declaration")]

    def set_declaration(self, obj):
        obj.declarations.append(self.declaration)
        self.declaration.owner = obj
        if self.declaration.type_ref is not None:
            obj.type_refs.append(self.declaration.type_ref)


class declaration_offset(CommonListener):
    def init_interfaces(self):
        self.actions = [
            Action(self.set_offset, "declaration_offset"),
        ]

    def set_offset(self, e):
        e.offset = self.children[0].value


class enumeration_expr(CommonListener):
    def init_interfaces(self):
        self.actions = [
            Action(self.set_value, "enumeration_expr"),
        ]

    def set_value(self, e):
        e.value = self.children[0].value


class enumeration_noprefix(CommonListener):
    def init_interfaces(self):
        self.actions = [
            Action(self.set_noprefix, "enumeration_noprefix"),
            Action(self.set_noprefix, "enumeration_attribute"),
        ]

    def set_noprefix(self, e):
        e.prefix = ''


class enumeration_explicit(CommonListener):
    def init_interfaces(self):
        self.actions = [
            Action(self.set_explicit, "enumeration_attribute"),
        ]

    def set_explicit(self, e):
        e.set_explicit()


class enumeration_constant(CommonListener):
    def init_interfaces(self):
        name = self.children[0]
        self.constant = EnumerationConstant(name)

        self.action_list.take_all(self.constant,
                                  ["enumeration_noprefix",
                                   "enumeration_expr"])
        self.action_list.remove_all(["enumeration_attribute"])

        self.actions = [
            Action(self.add_constant, "enumeration_constant"),
        ]

    def add_constant(self, d):
        d.add_enumerator(self.constant)


class bitfield_width(CommonListener):
    def init_interfaces(self):
        self.actions = [
            Action(self.get_width, "bitfield_width"),
        ]

    def get_width(self, bit):
        bit.width = self.children[0].value


class bitfield_bit_range(CommonListener):
    def init_interfaces(self):
        self.actions = [
            Action(self.get_bits, "bitfield_bit_range"),
        ]
        self.bit = int(self.children[0].value)
        assert(len(self.children) <= 2)
        if len(self.children) == 1:
            self.width = 1
        else:
            msb = self.bit
            self.bit = int(self.children[1].value)
            self.width = msb - self.bit + 1
            if self.width < 1:
                raise DSLError("invalid bitifield specfier", self.children[1])

    def get_bits(self, specifier):
        specifier.add_bit_range(self.bit, self.width)


class bitfield_auto(CommonListener):
    def init_interfaces(self):
        self.width = None
        self.actions = [
            Action(self.set_bitfield_auto, "bitfield_auto"),
        ]
        self.action_list.take(self, "bitfield_width")

    def set_bitfield_auto(self, specifier):
        specifier.set_type_auto(self.width)


class bitfield_others(CommonListener):
    def init_interfaces(self):
        self.actions = [
            Action(self.set_bitfield_others, "bitfield_others"),
        ]

    def set_bitfield_others(self, specifier):
        specifier.set_type_others()


class bitfield_delete(CommonListener):
    def init_interfaces(self):
        self.actions = [
            Action(self.bitfield_delete, "bitfield_delete"),
        ]

    def bitfield_delete(self, ext):
        ext.add_delete_member(self.children[0])


class bitfield_specifier(CommonListener):
    def init_interfaces(self):
        self.actions = [
            Action(self.set_specifier, "bitfield_specifier"),
        ]
        self.bitfield_specifier = BitFieldSpecifier()

        al = ["bitfield_bit_range", "bitfield_auto", "bitfield_others"]
        self.action_list.take_all(self.bitfield_specifier, al)

    def set_specifier(self, declaration):
        declaration.bitfield_specifier = self.bitfield_specifier


class bitfield_unknown(CommonListener):
    def init_interfaces(self):
        self.actions = [
            Action(self.set_bitfield_member, "bitfield_member"),
        ]

    def set_bitfield_member(self, declaration):
        declaration.member_name = self.children[0]
        declaration.set_ignored()


class bitfield_member(CommonListener):
    def init_interfaces(self):
        self.name = self.children[0]

        self.actions = [
            Action(self.set_bitfield_member, "bitfield_member"),
        ]

    def set_bitfield_member(self, declaration):
        declaration.member_name = self.name


class bitfield_const(CommonListener):
    def init_interfaces(self):
        self.actions = [
            Action(self.set_bitfield_member, "bitfield_member"),
        ]

    def set_bitfield_member(self, declaration):
        declaration.member_name = "<const>"


class bitfield_shift(CommonListener):
    def init_interfaces(self):
        self.actions = [
            Action(self.set_bitfield_shift, "bitfield_shift"),
        ]

    def set_bitfield_shift(self, declaration):
        return int(self.children[0].value)


class bitfield_default(CommonListener):
    def init_interfaces(self):
        self.action_list.take(self, 'constant_expression')
        self.default = self.children[0]
        self.actions = [
            Action(self.set_default, 'bitfield_default'),
        ]

    def set_default(self, obj):
        obj.default = self.default


class bitfield_declaration(CommonListener):
    def init_interfaces(self):
        if self.action_list.has("bitfield_delete"):
            return

        self.declaration = BitFieldDeclaration()

        shift = self.action_list.take(self.declaration, "bitfield_shift")

        al = ["bitfield_member", "bitfield_specifier", "bitfield_default",
              "direct_type", "primitive_type", "bitfield_type",
              "enumeration_type", "alternative_type"]

        self.action_list.take_all(self.declaration, al)

        if self.action_list.has("pointer_has_pointer"):
            raise DSLError("cannot declare a pointer member in bitfield",
                           self.declaration.member_name)

        if self.action_list.has("object_type_has_object"):
            raise DSLError("cannot declare an object type member in bitfield",
                           self.declaration.member_name)

        self.actions = [Action(self.set_declaration, "bitfield_declaration")]

        if shift:
            self.declaration.bitfield_specifier.set_type_shift(shift)

    def set_declaration(self, definition):
        definition.declarations.append(self.declaration)
        if self.declaration.type_ref is not None:
            definition.type_refs.append(self.declaration.type_ref)


class public(CommonListener):
    def init_interfaces(self):
        self.actions = [
            Action(self.set_public, self.__class__.__name__)
        ]

    def set_public(self, definition):
        definition.set_public()


class constant_definition(CommonListener):
    def init_interfaces(self):
        self.name = self.children[0]
        self.action_list.take(self, 'constant_expression')

        d = ConstantDefinition(self.name, self.children[-1].value)
        self.definition = d

        al = ["direct_type", "array", "pointer", "alias",
              "primitive_type", "bitfield_type", "structure_type",
              "union_type", "object_type", "enumeration_type",
              "alternative_type", "public"]
        self.action_list.take_all(self.definition, al)

        rl = ["pointer_has_pointer", "object_type_set_complex"]
        self.action_list.remove_all(rl)

        self.actions = [
            Action(self.set_definition, self.__class__.__name__),
            Action(self.set_type_ref, 'add_type_ref'),
        ]

    def set_definition(self, obj):
        obj.definitions.append(self.definition)

    def set_type_ref(self, obj):
        if self.definition.type_ref is not None:
            obj.type_refs.append(self.definition.type_ref)


class ITypeDefinition(CommonListener):
    def init_interfaces(self):
        self.name = self.children[0]
        self.definition = None
        self.actions = [Action(self.set_definition, self.__class__.__name__)]

    def set_definition(self, obj):
        obj.definitions.append(self.definition)
        obj.type_refs += self.definition.type_refs


class alternative_definition(ITypeDefinition):
    def init_interfaces(self):
        super().init_interfaces()

        # must have "_t" postfix
        name = self.name
        if name[-2:] != "_t":
            raise DSLError("Invalid type name.\n"
                           "Type name must have _t as postfix ", name)

        d = AlternativeDefinition(name)
        self.definition = d

        al = ["direct_type", "array", "pointer",
              "primitive_type", "bitfield_type", "structure_type",
              "object_type", "union_type", "enumeration_type", "public"]
        self.action_list.take_all(self.definition, al)

        # special case, should have only 1 type ref
        if d.type_ref is not None:
            d.type_refs.append(d.type_ref)

        rl = ["pointer_has_pointer"]
        self.action_list.remove_all(rl)


class bitfield_const_decl(CommonListener):
    def init_interfaces(self):
        self.actions = [Action(self.set_const, "bitfield_const_decl")]

    def set_const(self, obj):
        if obj.const:
            # TODO: proper logger warnings
            print("Warning: redundant bitfield const")
        obj.const = True


class bitfield_definition(ITypeDefinition):
    def init_interfaces(self):
        super().init_interfaces()

        if self.action_list.has("bitfield_delete"):
            raise DSLError(
                "delete only allowed in extend", self.name)

        self.size = -1
        self.const = False

        al = ["bitfield_size", "bitfield_const_decl"]
        self.action_list.take_all(self, al)

        d = BitFieldDefinition(self.name)
        d.type_name = self.name
        d.length = self.size
        d.const = self.const
        self.definition = d

        self.action_list.take(d, "public")

        self.action_list.take(d, "bitfield_declaration", single=False)
        d.update_unit_info()


class structure_definition(IABISpecific, ITypeDefinition):
    def init_interfaces(self):
        super().init_interfaces()

        d = StructureDefinition(self.name)
        self.definition = d

        self.action_list.take(d, "declaration", single=False)
        self.action_list.take(d, "qualifier_list")
        self.action_list.take(d, "public")


class union_definition(ITypeDefinition):
    def init_interfaces(self):
        super().init_interfaces()

        d = UnionDefinition(self.name)
        self.definition = d

        self.action_list.take(d, "declaration", single=False)
        self.action_list.take(d, "qualifier_list")
        self.action_list.take(d, "public")


class enumeration_definition(IABISpecific, ITypeDefinition):
    def init_interfaces(self):
        super().init_interfaces()

        d = EnumerationDefinition(self.name)
        self.definition = d
        self.action_list.take(d, "enumeration_attribute")
        self.action_list.take(d, "public")
        # Unused
        self.action_list.remove_all(["enumeration_noprefix"])

        self.action_list.take(d, "enumeration_constant", single=False)


class object_definition(IABISpecific, ITypeDefinition):
    def init_interfaces(self):
        super().init_interfaces()

        d = ObjectDefinition(self.name)
        self.definition = d

        self.action_list.take(d, "declaration", single=False)
        self.action_list.take(d, "qualifier_list")
        self.action_list.take(d, "public")

        rl = ["object_type_has_object"]
        self.action_list.remove_all(rl)


class module_name(CommonListener):
    def init_interfaces(self):
        self.module_name = self.children[0]
        self.actions = [Action(self.set_name, "module_name")]

    def set_name(self, extension):
        extension.module_name = self.module_name


class bitfield_size(CommonListener):
    def init_interfaces(self):
        self.size = int(self.children[0].value)
        self.actions = [Action(self.set_size, "bitfield_size")]

    def set_size(self, obj):
        obj.size = self.size


class ITypeExtension(CommonListener):
    def init_interfaces(self):
        # need to check definition if allowed it in the feature
        self.type_name = self.children[0]
        self.extension = None
        self.actions = [Action(self.set_extension, self.__class__.__name__)]

    def set_extension(self, parent):
        parent.type_refs.append(self.extension)
        parent.type_refs += self.extension.type_refs


class bitfield_extension(ITypeExtension):
    def init_interfaces(self):
        super().init_interfaces()

        e = BitFieldExtension(self.type_name)
        self.extension = e

        self.action_list.take(e, "bitfield_delete", single=False)

        al = ["bitfield_declaration", "module_name"]
        self.action_list.take_all(e, al)


class structure_extension(ITypeExtension):
    def init_interfaces(self):
        super().init_interfaces()

        e = StructureExtension(self.type_name)
        self.extension = e

        al = ["declaration", "module_name"]
        self.action_list.take_all(e, al)


class object_extension(ITypeExtension):
    def init_interfaces(self):
        super().init_interfaces()

        e = ObjectExtension(self.type_name)
        self.extension = e

        rl = ["object_type_has_object"]
        self.action_list.remove_all(rl)

        al = ["declaration", "module_name"]
        self.action_list.take_all(e, al)


class union_extension(ITypeExtension):
    def init_interfaces(self):
        super().init_interfaces()

        e = UnionExtension(self.type_name)
        self.extension = e

        if self.action_list.has("object_type_has_object"):
            raise DSLError("cannot declare an object type member in union",
                           self.declaration.member_name)

        al = ["declaration", "module_name"]
        self.action_list.take_all(e, al)


class enumeration_extension(ITypeExtension):
    def init_interfaces(self):
        super().init_interfaces()

        e = EnumerationExtension(self.type_name)
        self.extension = e

        al = ["enumeration_constant", "module_name"]
        self.action_list.take_all(e, al)


class object_type(CommonListener):
    def init_interfaces(self):
        self.type_name = self.children[-1]
        self.compound_type = ObjectType(self.type_name)

        self.actions = [
            Action(self.set_type, "object_type"),
            Action(self.create_declaration, "object_type_create_declaration"),
            Action(self.has_object, "object_type_has_object"),
            Action(self.set_complex, "object_type_set_complex"),
        ]

    def create_declaration(self, obj):
        d = ObjectDeclaration()
        d.type_ref = self.compound_type
        return d

    def set_type(self, declaration):
        declaration.compound_type = self.compound_type
        declaration.type_ref = declaration.compound_type
        declaration.is_customized_type = True

    def has_object(self, obj):
        return True

    def set_complex(self, obj):
        self.compound_type.complex_type = True


class object_noprefix(CommonListener):
    def init_interfaces(self):
        self.actions = [Action(self.set_noprefix, 'object_noprefix')]

    def set_noprefix(self, obj):
        obj.noprefix = True
