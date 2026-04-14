import os
import coverage
import subprocess
import shutil
import glob
import re
import filecmp


class CodegenTester:
    def __init__(self):
        # Base directories
        self.root_dir = os.path.relpath("../..")
        self.root_tools_dir = os.path.relpath("..")
        self.rule_fd = None
        self.TAB = " " * 4

        # Initialize directory structure
        self._init_directories()

        # Initialize settings and rules
        self._init_settings()
        self._init_rules()

        # Initialize test cases
        self._init_test_cases()

        # Tracking lists
        self.registers_preprocessed = []
        self.types_preprocessed = []
        self.hypercalls_preprocessed = []
        self.events_preprocessed_modules = []
        self.events_preprocessed_interfaces = []

    def _init_directories(self):
        """Initialize all directory paths"""
        # Types directories
        self.types_test_inputs_dir = os.path.join("types", "test_inputs")
        self.types_generated_dir = os.path.join("types", "generated")
        self.types_expected_output_dir = os.path.join(
            "types", "expected_output"
        )
        self.types_template_dir = os.path.join(
            self.root_dir, "hyp", "core", "base", "templates"
        )

        # Hypercalls directories
        self.hypercalls_test_input_dir = os.path.join(
            "hypercalls", "test_inputs"
        )
        self.hypercalls_expected_output_dir = os.path.join(
            "hypercalls", "expected_output"
        )
        self.hypercalls_generated_dir = os.path.join("hypercalls", "generated")
        self.hypercalls_template_dirs = [
            os.path.join(self.root_dir, "hyp", "core", "api", "templates"),
            os.path.join(
                self.root_dir, "hyp", "core", "api", "aarch64", "templates"
            ),
            os.path.join(
                self.root_dir, "hyp", "vm", "smccc", "aarch64", "templates"
            ),
            os.path.join(self.root_tools_dir, "hypercalls", "templates"),
        ]

        # Events directories
        self.events_test_input_modules_dir = os.path.join(
            "events", "test_inputs", "modules"
        )
        self.events_test_input_interfaces_dir = os.path.join(
            "events", "test_inputs", "interfaces"
        )
        self.events_expected_output_dir = os.path.join(
            "events", "expected_output"
        )
        self.events_generated_dir = os.path.join("events", "generated")
        self.events_template_dirs = os.path.join(
            self.root_tools_dir, "events", "templates"
        )

        # Registers directories
        self.registers_test_input_dir = os.path.join(
            "registers", "test_inputs"
        )
        self.registers_expected_output_dir = os.path.join(
            "registers", "expected_output"
        )
        self.registers_generated_dir = os.path.join("registers", "generated")
        self.registers_template_dirs = [
            os.path.join(self.root_dir, "hyp", "arch", "aarch64", "templates"),
        ]

        # Objects directories
        self.objects_expected_output_dir = os.path.join(
            "objects", "expected_output"
        )
        self.objects_generated_dir = os.path.join("objects", "generated")
        self.objects_expected_output_dir = os.path.join(
            "objects", "expected_output"
        )
        self.objects_template_dirs = {
            "cspace_twolevel": [
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "core",
                    "cspace_twolevel",
                    "templates",
                    "object.ev.tmpl",
                ),
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "core",
                    "cspace_twolevel",
                    "templates",
                    "object.c.tmpl",
                ),
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "core",
                    "cspace_twolevel",
                    "templates",
                    "cspace_lookup.c.tmpl",
                ),
            ],
            "object_standard": [
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "core",
                    "object_standard",
                    "templates",
                    "object.c.tmpl",
                ),
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "core",
                    "object_standard",
                    "templates",
                    "object.tc.tmpl",
                ),
            ],
            "partition_standard": [
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "core",
                    "partition_standard",
                    "templates",
                    "object.ev.tmpl",
                ),
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "core",
                    "partition_standard",
                    "templates",
                    "object.tc.tmpl",
                ),
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "core",
                    "partition_standard",
                    "templates",
                    "object.c.tmpl",
                ),
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "core",
                    "partition_standard",
                    "templates",
                    "hypercalls.c.tmpl",
                ),
            ],
            "object_lists": [
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "debug",
                    "object_lists",
                    "templates",
                    "object_lists.tc.tmpl",
                ),
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "debug",
                    "object_lists",
                    "templates",
                    "object_lists.ev.tmpl",
                ),
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "debug",
                    "object_lists",
                    "templates",
                    "object_lists.c.tmpl",
                ),
            ],
            "cspace_interface": [
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "interfaces",
                    "cspace",
                    "templates",
                    "cspace_lookup.h.tmpl",
                ),
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "interfaces",
                    "cspace",
                    "templates",
                    "cspace.tc.tmpl",
                ),
            ],
            "object_interface": [
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "interfaces",
                    "object",
                    "templates",
                    "object.tc.tmpl",
                ),
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "interfaces",
                    "object",
                    "templates",
                    "object.ev.tmpl",
                ),
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "interfaces",
                    "object",
                    "templates",
                    "object.h.tmpl",
                ),
            ],
            "partition_interface": [
                os.path.join(
                    self.root_dir,
                    "hyp",
                    "interfaces",
                    "partition",
                    "templates",
                    "partition_alloc.h.tmpl",
                ),
            ],
        }

        # Define common files
        self.rules_file = os.path.join("build.ninja")

        self.types_pickle = os.path.join(
            self.types_generated_dir, "basic_types", "types.pickle"
        )
        self.types_header = os.path.join(
            self.types_generated_dir, "basic_types", "hyptypes.h"
        )
        self.types_guest_header = os.path.join(
            self.types_generated_dir, "basic_types", "guest_header.h"
        )

    def _init_settings(self):
        """Initialize all settings"""
        # Get toolchain path from environment variable
        llvm = os.environ.get("LLVM")
        if not llvm:
            # Try QCOM_LLVM
            llvm = os.environ.get("QCOM_LLVM")
        if llvm:
            # Remove trailing slash if present
            llvm = llvm.rstrip("/")
            clang_path = f"{llvm}/bin/clang"
            formatter_path = f"{llvm}/bin/clang-format"
        else:
            # Fallback to default paths if environment variables not set
            clang_path = "clang"
            formatter_path = (
                "clang-format"
            )

        self.complete_settings = {
            "CLANG": clang_path,
            "FORMATTER": formatter_path,
            "TARGET_TRIPLE": "aarch64-linux-gnu",
            "TYPES": f"{self.root_tools_dir}/typed/type_gen.py",
            "HYPERCALLS": f"{self.root_tools_dir}/hypercalls/hypercall_gen.py",
            "EVENTS": f"{self.root_tools_dir}/events/event_gen.py",
            "REGISTERS": f"{self.root_tools_dir}/registers/register_gen.py",
            "OBJECTS": f"{self.root_tools_dir}/objects/object_gen.py",
        }

        self.derived_settings = {
            "CPP": "${CLANG}-cpp -target ${TARGET_TRIPLE}",
        }

    def _init_rules(self):
        """Initialize all build rules"""
        # Common rules
        self.cpp_dsl_rule = {
            "cpp-dsl": {
                "command": (
                    "${CPP} -nostdlibinc -undef $DSL_DEFINES -x c -P -MD -MF"
                    " ${out}.d -MT ${out} ${in} > ${out}"
                ),
                "description": "cpp-dsl ${out}",
                "depfile": "${out}.d",
            }
        }

        # Types Rules
        self.types_rules = {
            "types_parse": {
                "command": "${TYPES} -a ${ABI} -d ${out}.d ${in} -P ${out}",
                "description": "types_parse ${out}",
                "depfile": "${out}.d",
            },
            "gen_types": {
                "command": (
                    "${TYPES} -a ${ABI} -f ${FORMATTER} -d ${out}.d -p ${in}"
                    " -o ${out}"
                ),
                "description": "gen_types ${out}",
                "depfile": "${out}.d",
            },
            "gen_public_types": {
                "command": (
                    "${TYPES} --public -a ${ABI} -f ${FORMATTER} -d ${out}.d"
                    " -p ${in} -o ${out}"
                ),
                "description": "gen_public_types ${out}",
                "depfile": "${out}.d",
            },
            "gen_types_tmpl": {
                "command": (
                    "${TYPES} -a ${ABI} -f ${FORMATTER} -d ${out}.d -t"
                    " ${TEMPLATE} -p ${in} -o ${out}"
                ),
                "description": "gen_types_tmpl ${out}",
                "depfile": "${out}.d",
            },
        }

        # Hypercalls Rules
        self.hypercalls_rules = {
            "hypercall_gen": {
                "command": (
                    "LANG=C.UTF-8 ${HYPERCALLS} -a ${ABI} -f ${FORMATTER} -d"
                    " ${out}.d -t ${TEMPLATE} -p ${TYPES_PICKLE} ${in} -o"
                    " ${out}"
                ),
                "description": "hypercalls_gen ${out}",
                "depfile": "${out}.d",
            },
        }

        # Events Rules
        self.events_rules = {
            "event_parse": {
                "command": "${EVENTS} ${INCLUDES} -d ${out}.d ${in} -P ${out}",
                "description": "event_parse ${out}",
                "depfile": "${out}.d",
                "restat": "true",
            },
            "event_gen": {
                "command": (
                    "${EVENTS} -t ${TEMPLATE} -f ${FORMATTER} -m ${MODULE}"
                    " ${OPTIONS}${INCLUDES} -d ${out}.d -p ${in} -o ${out}"
                ),
                "description": "event_gen ${out}",
                "depfile": "${out}.d",
                "restat": "true",
            },
        }
        # Registers Rules
        self.registers_rules = {
            "registers_gen": {
                "command": (
                    "${REGISTERS} -t ${TEMPLATE} -f ${FORMATTER} -o ${out} -p"
                    " ${TYPES_PICKLE} ${in}"
                ),
                "description": "registers_gen ${out}",
            }
        }

        # Objects Rules
        self.objects_rules = {
            "object_gen": {
                "command": "${OBJECTS} -t ${in} ${OBJ} -o ${out}",
                "description": "object_gen ${out}",
            },
            "object_gen_c": {
                "command": (
                    "${OBJECTS} -t ${in} -f ${FORMATTER} ${OBJ} -o ${out}"
                ),
                "description": "object_gen_c ${out}",
            },
        }

    def _init_test_cases(self):
        """Initialize test cases"""
        # ============================================================================
        # Register Test Cases
        # ============================================================================

        self.input_registers = [
            {
                "id": "REG-001",
                "name": "basic_registers",
                "description": (
                    "Test basic register definitions and code generation"
                ),
                "expected_exit_code": 0,
                "expected_error": None,
                "expected_output_files": ["hypregisters.h"],
            },
            {
                "id": "REG-002",
                "name": "invalid_access_type_register",
                "description": "Test error handling for invalid access type",
                "expected_exit_code": 1,
                "expected_error": "Invalid access type '.*'",
            },
            {
                "id": "REG-003",
                "name": "invalid_type_register",
                "description": "Test error handling for invalid access type",
                "expected_exit_code": 1,
                "expected_error": "Exception: no type: .* for register .*",
            },
        ]

        # ============================================================================
        # Event Test Cases
        # ============================================================================

        self.input_events = [
            {
                "id": "EVT-001",
                "name": "basic_event_features",
                "description": (
                    "Test basic event features including triggers and handlers"
                ),
                "expected_exit_code": 0,
                "expected_error": None,
                "expected_output_files": [
                    "basic_event_features_interface_triggers.h",
                    "basic_event_features_interface.c",
                    "basic_event_features_module_handlers.h",
                ],
            },
            {
                "id": "EVT-002",
                "name": "invalid_event_name",
                "description": "Test error handling for invalid event names",
                "expected_exit_code": 1,
                "expected_error": "error: incorrect name:.*",
            },
            {
                "id": "EVT-003",
                "name": "previously_released_lock",
                "description": (
                    "Test error detection for locks that were previously"
                    " released"
                ),
                "expected_exit_code": 1,
                "expected_error": r"error: \(.*\) previously released",
            },
            {
                "id": "EVT-004",
                "name": "event_subscribe_invalid",
                "description": (
                    "Test error handling for subscription to unknown events"
                ),
                "expected_exit_code": 1,
                "expected_error": "error: subscribed to unknown event.*",
            },
            {
                "id": "EVT-005",
                "name": "duplicate_def",
                "description": (
                    "Test error detection for duplicate event definitions"
                ),
                "expected_exit_code": 1,
                "expected_error": "error: duplicate definition of event .*",
            },
            {
                "id": "EVT-006",
                "name": "inconsistent_lock_kinds",
                "description": (
                    "Test error detection for inconsistent lock kinds"
                ),
                "expected_exit_code": 1,
                "expected_error": "error: inconsistent lock kinds for .*",
            },
            {
                "id": "EVT-007",
                "name": "selector_event_with_priority",
                "description": (
                    "Test error handling for priority specification on"
                    " selector events"
                ),
                "expected_exit_code": 1,
                "expected_error": (
                    r"error: priority \(.*\) cannot be specified for"
                    " subscription to a selector event"
                ),
            },
            {
                "id": "EVT-008",
                "name": "duplicate_selector",
                "description": (
                    "Test error detection for duplicate selectors in selector"
                    " events"
                ),
                "expected_exit_code": 1,
                "expected_error": (
                    "error: duplicate selector '.*' specified for subscription"
                    " to selector event '.*'"
                ),
            },
            {
                "id": "EVT-009",
                "name": "noreturn_not_last",
                "description": (
                    "Test error detection for noreturn handlers that are not"
                    " last"
                ),
                "expected_exit_code": 1,
                "expected_error": (
                    "error: handler .* for event .* does not return"
                ),
            },
            {
                "id": "EVT-010",
                "name": "previously_acquired_lock",
                "description": (
                    "Test error detection for locks that were previously"
                    " acquired"
                ),
                "expected_exit_code": 1,
                "expected_error": "error: (.*) previously acquired at .*",
            },
            {
                "id": "EVT-011",
                "name": "result_param_name",
                "description": (
                    "Test error handling for setup events with explicit"
                    " 'result' parameter"
                ),
                "expected_exit_code": 1,
                "expected_error": (
                    "error: setup event must not have an explicit parameter"
                    " named 'result'"
                ),
            },
        ]

        # ============================================================================
        # Hypercall Test Cases
        # ============================================================================

        self.input_hypercalls = [
            {
                "id": "HYP-001",
                "name": "basic_hypercalls",
                "description": (
                    "Test basic hypercall definitions with single and multiple"
                    " inputs/outputs"
                ),
                "expected_exit_code": 0,
                "expected_error": None,
                "expected_output_files": [
                    "c_wrapper.c",
                    "guest_interface.c",
                    "guest_interface.h",
                    "hyp_wrapper.c",
                    "hypcall_def.h",
                    "hypcall_table.S",
                ],
            },
            {
                "id": "HYP-002",
                "name": "already_used_name_hypercall",
                "description": (
                    "Test error handling for hypercalls with already used name"
                ),
                "expected_exit_code": 1,
                "expected_error": "Hypercall name: .* already used",
            },
            {
                "id": "HYP-003",
                "name": "already_used_call_num_hypercall",
                "description": (
                    "Test error handling for hypercalls with already used"
                    " call_num"
                ),
                "expected_exit_code": 1,
                "expected_error": r"Hypercall \d+ \(.*\) already used",
            },
        ]

        # ============================================================================
        # Object Test Cases
        # ============================================================================

        self.input_objects = [
            {
                "id": "OBJ-001",
                "name": "basic_objects",
                "description": (
                    "Test basic object definitions and code generation for"
                    " various object types"
                ),
                "obj_list": (
                    "addrspace cspace doorbell"
                    " gicv3_its,no_partition_create_hypcall"
                    " hwirq,no_partition_create_hypcall,no_object_list"
                    " memextent msgqueue"
                    " partition,no_partition_create_hypcall,no_object_list"
                    " thread vic virtio_backend vpm_group vrtc"
                ),
                "expected_exit_code": 0,
                "expected_error": None,
                "expected_output_files": {
                    "cspace_twolevel": [
                        "object.ev",
                        "object.c",
                        "cspace_lookup.c",
                    ],
                    "object_standard": ["object.c", "object.tc"],
                    "partition_standard": [
                        "object.ev",
                        "object.tc",
                        "object.c",
                        "hypercalls.c",
                    ],
                    "object_lists": [
                        "object_lists.tc",
                        "object_lists.ev",
                        "object_lists.c",
                    ],
                    "cspace_interface": ["cspace_lookup.h", "cspace.tc"],
                    "object_interface": ["object.tc", "object.ev", "object.h"],
                    "partition_interface": ["partition_alloc.h"],
                },
            },
        ]

        # ============================================================================
        # Type Test Cases
        # ============================================================================

        self.input_types = [
            {
                "id": "TYP-001",
                "name": "basic_types",
                "description": (
                    "Test type definitions including newtypes, enumerations,"
                    " structures, and bitfields"
                ),
                "expected_exit_code": 0,
                "expected_error": None,
                "expected_output_files": [
                    "accessors.c",
                    "guest_header.h",
                    "hypconstants.h",
                    "hypcontainers.h",
                    "hypresult.c",
                    "hypresult.h",
                    "hyptypes.h",
                ],
            },
            {
                "id": "TYP-002",
                "name": "self_referential_constant",
                "description": (
                    "Test error detection for self referential constant define"
                ),
                "expected_exit_code": 1,
                "expected_error": (
                    "Error: Definition of constant is self-referential"
                ),
            },
            {
                "id": "TYP-003",
                "name": "illegal_lockable_type",
                "description": (
                    "Test error detection for illegal lockable type define"
                ),
                "expected_exit_code": 1,
                "expected_error": (
                    "Error: Only structure, object and union definitions may"
                    " be lockable"
                ),
            },
            {
                "id": "TYP-004",
                "name": "missing_definition",
                "description": (
                    "Test error detection for a missing type definition"
                    " reference"
                ),
                "expected_exit_code": 1,
                "expected_error": (
                    "Error: Failed to find corresponding definition for .*"
                ),
            },
            {
                "id": "TYP-005",
                "name": "invalid_name_type",
                "description": (
                    "Test error detection for a new type definition with an"
                    " invalid name"
                ),
                "expected_exit_code": 1,
                "expected_error": (
                    "Error: Invalid type name.\nType name must have _t as"
                    " postfix"
                ),
            },
            {
                "id": "TYP-006",
                "name": "delete_not_in_extend",
                "description": (
                    "Test error detection for a delete field of a bitfield"
                    " type, not in an extend clause"
                ),
                "expected_exit_code": 1,
                "expected_error": "Error: delete only allowed in extend",
            },
            {
                "id": "TYP-007",
                "name": "bitfield_bits_conflict",
                "description": (
                    "Test error detection for a bitfield type definition with"
                    " conflicting bitfields"
                ),
                "expected_exit_code": 1,
                "expected_error": (
                    "Error: bitfield member conflicts with previously"
                    " specified bits"
                ),
            },
        ]

    def writeline(self, line="\n"):
        """Write a line to the rule file"""
        self.rule_fd.writelines(line + "\n")

    def setup_environment(self):
        """Write environment settings to the rule file"""
        for key in self.complete_settings:
            self.writeline(f"{key} = {self.complete_settings[key]}")
        self.writeline()
        for key in self.derived_settings:
            self.writeline(f"{key} = {self.derived_settings[key]}")
        self.writeline()

    def gen_rules_from_dict(self, rule_dict):
        """Generate rules from a dictionary"""
        for rule in rule_dict:
            self.writeline(f"rule {rule}")
            for rule_part_key in rule_dict[rule]:
                line = f"{rule_part_key} = {rule_dict[rule][rule_part_key]}"
                self.writeline(self.TAB + line)
            self.writeline()

    def gen_rules(self):
        """Generate all rules"""
        self.setup_environment()
        self.gen_rules_from_dict(self.cpp_dsl_rule)
        self.gen_rules_from_dict(self.types_rules)
        self.gen_rules_from_dict(self.hypercalls_rules)
        self.gen_rules_from_dict(self.events_rules)
        self.gen_rules_from_dict(self.registers_rules)
        self.gen_rules_from_dict(self.objects_rules)

    def add_dsl_target(self, target_file, src_file, DSL_DEFINE):
        """Add a DSL target to the rule file"""
        line = f"build {target_file} : cpp-dsl {src_file}\n"
        line += self.TAB + f"DSL_DEFINES = -D__{DSL_DEFINE}_DSL__"
        self.writeline(line)

    # Types generation methods
    def gen_types_pp_targets(self, type_input):
        """Generate preprocessed targets for types"""
        self.types_preprocessed.clear()
        types_generated_dir_single = os.path.join(
            self.types_generated_dir, type_input
        )
        types_test_inputs_dir_single = os.path.join(
            self.types_test_inputs_dir, type_input
        )
        os.makedirs(types_generated_dir_single, exist_ok=True)
        types_test_files = os.listdir(types_test_inputs_dir_single)
        for file in types_test_files:
            ext = os.path.splitext(file)[1]
            if ext != ".tc":
                continue

            pp_file = f"{types_generated_dir_single}/{file}.pp"
            src_file = f"{types_test_inputs_dir_single}/{file}"
            self.add_dsl_target(pp_file, src_file, "TYPED")
            self.types_preprocessed.append(pp_file)
        self.writeline()
        return types_generated_dir_single

    def gen_types_pkl_target(self, types_generated_dir_single):
        """Generate pickle target for types"""
        types_pickle_single = os.path.join(
            types_generated_dir_single, "types.pickle"
        )

        line = (
            f"build {types_pickle_single} | {types_pickle_single}.d :"
            " types_parse "
        )
        line += (
            " ".join(self.types_preprocessed)
            + " || "
            + types_generated_dir_single
            + "\n"
        )
        line += self.TAB + "ABI = aarch64"
        self.writeline(line)
        return types_pickle_single

    def gen_types_header(
        self, types_generated_dir_single, types_pickle_single
    ):
        """Generate header target for types"""
        types_header_single = os.path.join(
            types_generated_dir_single, "hyptypes.h"
        )

        line = (
            f"build {types_header_single} | {types_header_single}.d :"
            f" gen_types {types_pickle_single} ||"
            f" {types_generated_dir_single}\n"
        )
        line += self.TAB + "ABI = aarch64"
        self.writeline(line)

    def gen_types_guest_header_target(
        self, types_generated_dir_single, types_pickle_single
    ):
        """Generate guest header target for types"""
        types_guest_header_single = os.path.join(
            types_generated_dir_single, "guest_header.h"
        )
        line = (
            f"build {types_guest_header_single} |"
            f" {types_guest_header_single}.d : gen_public_types"
            f" {types_pickle_single} || {types_generated_dir_single}\n"
        )
        line += self.TAB + "ABI = aarch64"
        self.writeline(line)

    def gen_types_headers_target(
        self, types_generated_dir_single, types_pickle_single
    ):
        """Generate headers target for types"""
        types_templates = os.listdir(self.types_template_dir)
        for tmpl in types_templates:
            file = tmpl[: tmpl.find(".tmpl")]
            line = (
                f"build {types_generated_dir_single}/{file} |"
                f" {types_generated_dir_single}/{file}.d : gen_types_tmpl"
                f" {types_pickle_single} | {self.types_template_dir}/{tmpl} ||"
                f" {types_generated_dir_single}\n"
            )
            line += self.TAB + "ABI = aarch64" + "\n"
            line += self.TAB + f"TEMPLATE = {self.types_template_dir}/{tmpl}"
            self.writeline(line)
        self.writeline()

    # Hypercalls generation methods
    def gen_hypercalls_pp_targets(self, hypercall_file):
        """Generate preprocessed targets for hypercalls"""
        hypercalls_generated_dir_single = os.path.join(
            self.hypercalls_generated_dir, hypercall_file
        )

        os.makedirs(hypercalls_generated_dir_single, exist_ok=True)
        self.hypercalls_preprocessed.clear()
        file = hypercall_file + ".hvc"

        pp_file = f"{hypercalls_generated_dir_single}/{file}.pp"
        src_file = f"{self.hypercalls_test_input_dir}/{file}"
        self.add_dsl_target(pp_file, src_file, "HYPERCALLS")
        self.hypercalls_preprocessed.append(pp_file)

        self.writeline()

    def gen_hypercalls_table_target(self, hypercall_file):
        """Generate table target for hypercalls"""
        hypercalls_generated_dir_single = os.path.join(
            self.hypercalls_generated_dir, hypercall_file
        )
        for hypercalls_template_dir in self.hypercalls_template_dirs:
            try:
                hypercall_templates = os.listdir(hypercalls_template_dir)
                for tmpl in hypercall_templates:
                    file = tmpl[: tmpl.find(".tmpl")]
                    gen_file = f"{hypercalls_generated_dir_single}/{file}"
                    hypercall_pp_files = " ".join(self.hypercalls_preprocessed)
                    template = f"{hypercalls_template_dir}/{tmpl}"
                    line = (
                        f"build {gen_file} | {gen_file}.d : hypercall_gen"
                        f" {hypercall_pp_files} | {self.types_pickle}"
                        f" {template} || {self.hypercalls_generated_dir}\n"
                    )
                    line += self.TAB + "ABI = aarch64" + "\n"
                    line += self.TAB + f"TEMPLATE = {template}" + "\n"
                    line += self.TAB + f"TYPES_PICKLE = {self.types_pickle}"
                    self.writeline(line)
            except FileNotFoundError:
                print(
                    f"Warning: Template directory {hypercalls_template_dir}"
                    " not found"
                )
        self.writeline()

    # Events generation methods
    def gen_events_pp_target(self, event_name):
        """Generate preprocessed targets for events"""
        self.events_preprocessed_modules.clear()
        self.events_preprocessed_interfaces.clear()

        event_generated_dir_single = os.path.join(
            self.events_generated_dir, event_name
        )
        os.makedirs(event_generated_dir_single, exist_ok=True)

        ev_module_file = event_name + "_module.ev"
        ev_interface_file = event_name + "_interface.ev"

        # module
        ev_file_path = f"{self.events_test_input_modules_dir}/{ev_module_file}"
        pp_file_path = f"{event_generated_dir_single}/{ev_module_file}.pp"
        self.add_dsl_target(pp_file_path, ev_file_path, "EVENTS")
        self.events_preprocessed_modules.append(pp_file_path)

        # interface
        ev_file_path = (
            f"{self.events_test_input_interfaces_dir}/{ev_interface_file}"
        )
        pp_file_path = f"{event_generated_dir_single}/{ev_interface_file}.pp"
        self.add_dsl_target(pp_file_path, ev_file_path, "EVENTS")
        self.events_preprocessed_interfaces.append(pp_file_path)
        self.writeline()

    def gen_events_pkl_target(self, event_name):
        """Generate pickle target for events"""
        event_generated_dir_single = os.path.join(
            self.events_generated_dir, event_name
        )
        self.events_pickle = os.path.join(
            event_generated_dir_single, "events.pickle"
        )

        line = (
            f"build {self.events_pickle} | {self.events_pickle}.d :"
            " event_parse "
        )
        line += (
            " ".join(self.events_preprocessed_modules)
            + " "
            + " ".join(self.events_preprocessed_interfaces)
            + " || "
            + event_generated_dir_single
            + "\n"
        )
        self.writeline(line)

    def gen_events_source_target(self, event_name):
        """Generate source target for events"""
        ev_module_file = event_name + "_module.ev"
        ev_interface_file = event_name + "_interface.ev"

        event_generated_dir_single = os.path.join(
            self.events_generated_dir, event_name
        )

        handler_templates = ["handlers.h.tmpl"]
        trigger_templates = [
            "triggers.h.tmpl",
            "c.tmpl",
        ]

        for template in handler_templates:
            tmpl_name = os.path.splitext(template)[0]
            tmpl_path = f"{self.events_template_dirs}/{template}"
            module_name = os.path.splitext(ev_module_file)[0]
            out_file = (
                f"{event_generated_dir_single}/{module_name}_{tmpl_name}"
            )
            line = (
                f"build {out_file} | {out_file}.d : event_gen"
                f" {self.events_pickle} | {tmpl_path} ||"
                f" {event_generated_dir_single}\n"
            )
            line += self.TAB + f"MODULE = {module_name}\n"
            line += self.TAB + f"TEMPLATE = {tmpl_path}"
            self.writeline(line)

        for template in trigger_templates:
            tmpl_name = os.path.splitext(template)[0]
            tmpl_path = f"{self.events_template_dirs}/{template}"
            interface_name = os.path.splitext(ev_interface_file)[0]
            ext = os.path.splitext(tmpl_name)[1]
            if ext == ".h":
                out_file = (
                    f"{event_generated_dir_single}/"
                    f"{interface_name}_{tmpl_name}"
                )
            else:
                out_file = (
                    f"{event_generated_dir_single}/"
                    f"{interface_name}.{tmpl_name}"
                )
            line = (
                f"build {out_file} | {out_file}.d : event_gen"
                f" {self.events_pickle} | {tmpl_path} ||"
                f" {event_generated_dir_single}\n"
            )
            line += self.TAB + f"MODULE = {interface_name}\n"
            line += self.TAB + f"TEMPLATE = {tmpl_path}"
            self.writeline(line)

        self.writeline()

    # Registers generation methods
    def gen_registers_pp_targets(self, register_file):
        """Generate preprocessed targets for registers"""
        register_generated_dir_single = os.path.join(
            self.registers_generated_dir, register_file
        )

        os.makedirs(register_generated_dir_single, exist_ok=True)
        self.registers_preprocessed.clear()
        file = register_file + ".reg"

        pp_file = f"{register_generated_dir_single}/{file}.pp"
        src_file = f"{self.registers_test_input_dir}/{file}"
        self.add_dsl_target(pp_file, src_file, "REGISTERS")
        self.registers_preprocessed.append(pp_file)

        self.writeline()

    def gen_registers_header_target(self, register_file):
        """Generate table target for registers"""
        register_generated_dir_single = os.path.join(
            self.registers_generated_dir, register_file
        )
        for registers_template_dir in self.registers_template_dirs:
            try:
                register_templates = os.listdir(registers_template_dir)
                for tmpl in register_templates:
                    file = tmpl[: tmpl.find(".tmpl")]
                    gen_file = f"{register_generated_dir_single}/{file}"
                    register_pp_files = " ".join(self.registers_preprocessed)
                    template = f"{registers_template_dir}/{tmpl}"
                    line = (
                        f"build {gen_file} | {gen_file}.d : registers_gen"
                        f" {register_pp_files} | {self.types_pickle}"
                        f" {template} || {register_generated_dir_single}\n"
                    )
                    line += self.TAB + f"TEMPLATE = {template}" + "\n"
                    line += self.TAB + f"TYPES_PICKLE = {self.types_pickle}"
                    self.writeline(line)
            except FileNotFoundError:
                print(
                    f"Warning: Template directory {registers_template_dir} not"
                    " found"
                )
        self.writeline()

    # Object generation methods

    def gen_objects_targets(self, object_test):
        """Generate targets for objects"""
        obj_name = object_test["name"]
        obj_list = object_test["obj_list"]

        objects_generated_dir_single = os.path.join(
            self.objects_generated_dir, obj_name
        )
        os.makedirs(objects_generated_dir_single, exist_ok=True)

        for group_name, templates in self.objects_template_dirs.items():
            group_output_dir = os.path.join(
                objects_generated_dir_single, group_name
            )
            os.makedirs(group_output_dir, exist_ok=True)

            for template_path in templates:
                if not os.path.exists(template_path):
                    print(
                        f"Warning: Template {template_path} not found,"
                        " skipping"
                    )
                    continue

                template_name = os.path.basename(template_path)
                output_name = template_name.replace(".tmpl", "")
                output_path = os.path.join(group_output_dir, output_name)

                ext = os.path.splitext(output_name)[1]
                if ext in [".c", ".h"]:
                    rule = "object_gen_c"
                else:
                    rule = "object_gen"

                line = (
                    f"build {output_path} : {rule} {template_path} |"
                    f" {self.complete_settings['OBJECTS']} ||"
                    f" {group_output_dir}\n"
                )
                line += self.TAB + f"OBJ = {obj_list}"
                self.writeline(line)

        self.writeline()

    def format_directory_files(self, directory):
        """Format all source files in a specific directory"""
        formatter = self.complete_settings.get("FORMATTER")
        if not formatter:
            print("Error: Formatter not configured")
            return False

        if not os.path.exists(directory):
            return True

        # Define file extensions to format
        format_extensions = [".h", ".c"]

        formatted_files = 0
        failed_files = 0

        for root, _, files in os.walk(directory):
            for file in files:
                file_path = os.path.join(root, file)
                _, ext = os.path.splitext(file_path)

                if ext.lower() in format_extensions:
                    try:
                        print(f"    Formatting: {file_path}")
                        _ = subprocess.run(
                            [
                                formatter,
                                "-i",
                                file_path,
                            ],
                            capture_output=True,
                            text=True,
                            check=True,
                        )
                        formatted_files += 1
                    except subprocess.CalledProcessError as e:
                        print(f"    Failed to format {file_path}: {e}")
                        print(f"    Error output: {e.stderr}")
                        failed_files += 1

        if formatted_files > 0:
            print(f"    Formatted {formatted_files} files")

        return failed_files == 0

    def run_ninja_test(self, test_case, generated_dir, expected_output_dir):
        test_id = test_case.get("id", "UNKNOWN")
        name = test_case["name"]
        description = test_case.get("description", "No description provided")
        expected_exit_code = test_case["expected_exit_code"]
        expected_error = test_case["expected_error"]

        print(f"  [{test_id}] {description}")

        try:
            result = subprocess.run(["ninja"], capture_output=True, text=True)
            if result.returncode != expected_exit_code:
                print(
                    f"    FAILED: Expected exit code {expected_exit_code}, got"
                    f" {result.returncode}"
                )
                print("    Error:", result.stdout)
                return False

            if expected_exit_code != 0 and expected_error:
                if not re.search(expected_error, result.stdout):
                    print(
                        "    FAILED: Expected error message"
                        f" '{expected_error}' not found"
                    )
                    print("    Error output:", result.stdout)
                    return False

            # Format generated files before comparing
            if expected_exit_code == 0:
                # Format generated files
                test_dir = os.path.join(generated_dir, name)
                if os.path.exists(test_dir):
                    print("    Formatting generated files...")
                    self.format_directory_files(test_dir)

            if (
                expected_exit_code == 0
                and "expected_output_files" in test_case
            ):
                gen_files = test_case["expected_output_files"]

                for gen_file in gen_files:
                    generated_file_path = os.path.join(
                        generated_dir, name, gen_file
                    )
                    expected_output_file_path = os.path.join(
                        expected_output_dir, name, gen_file
                    )

                    try:
                        are_identical = filecmp.cmp(
                            generated_file_path,
                            expected_output_file_path,
                            shallow=False,
                        )
                        if not are_identical:
                            print(
                                "    FAILED: Generated file"
                                f" {generated_file_path} doesn't match"
                                " expected output"
                            )
                            return False
                    except FileNotFoundError:
                        print(
                            "    FAILED: File not found when comparing"
                            f" {generated_file_path} and"
                            f" {expected_output_file_path}"
                        )
                        return False

            print(f"    PASSED: {name}")
            return True

        except FileNotFoundError:
            print(
                "    ERROR: ninja command not found. Make sure it's installed"
                " and in your PATH."
            )
            return False

    def test_events(self):
        print("\n=== Event Tests ===")
        results = []
        for event_test in self.input_events:
            event_name = event_test["name"]

            print(f"Testing event: {event_name}")

            with open(self.rules_file, "w") as f:
                self.rule_fd = f
                self.gen_rules()
                self.gen_events_pp_target(event_name)
                self.gen_events_pkl_target(event_name)
                self.gen_events_source_target(event_name)

            results.append(
                self.run_ninja_test(
                    event_test,
                    self.events_generated_dir,
                    self.events_expected_output_dir,
                )
            )
        return results

    def test_registers(self):
        print("\n=== Register Tests ===")
        results = []
        for register in self.input_registers:
            register_name = register["name"]

            print(f"Testing Registers: {register_name}")

            with open(self.rules_file, "w") as f:
                self.rule_fd = f
                self.gen_rules()
                self.gen_registers_pp_targets(register_name)
                self.gen_registers_header_target(register_name)

            results.append(
                self.run_ninja_test(
                    register,
                    self.registers_generated_dir,
                    self.registers_expected_output_dir,
                )
            )
        return results

    def test_types(self):
        print("\n=== Type Tests ===")
        results = []
        for input_type in self.input_types:
            input_name = input_type["name"]

            with open(self.rules_file, "w") as f:
                self.rule_fd = f
                self.gen_rules()
                type_gen_dir_single = self.gen_types_pp_targets(input_name)
                types_pickle_single = self.gen_types_pkl_target(
                    type_gen_dir_single
                )
                self.gen_types_header(type_gen_dir_single, types_pickle_single)
                self.gen_types_guest_header_target(
                    type_gen_dir_single, types_pickle_single
                )
                self.gen_types_headers_target(
                    type_gen_dir_single, types_pickle_single
                )

            results.append(
                self.run_ninja_test(
                    input_type,
                    self.types_generated_dir,
                    self.types_expected_output_dir,
                )
            )
        return results

    def test_hypercalls(self):
        print("\n=== Hypercall Tests ===")
        results = []
        for hypercall_test in self.input_hypercalls:
            hypercall_name = hypercall_test["name"]

            print(f"Testing hypercall file: {hypercall_name}")

            with open(self.rules_file, "w") as f:
                self.rule_fd = f
                self.gen_rules()
                self.gen_hypercalls_pp_targets(hypercall_name)
                self.gen_hypercalls_table_target(hypercall_name)

            results.append(
                self.run_ninja_test(
                    hypercall_test,
                    self.hypercalls_generated_dir,
                    self.hypercalls_expected_output_dir,
                )
            )
        return results

    def test_objects(self):
        print("\n=== Object Tests ===")
        results = []
        for object_test in self.input_objects:
            obj_name = object_test["name"]
            test_id = object_test.get("id", "UNKNOWN")
            desc = object_test.get("description", "No description provided")

            print(f"  [{test_id}] {desc}")

            print(f"Testing objects: {obj_name}")

            with open(self.rules_file, "w") as f:
                self.rule_fd = f
                self.gen_rules()
                self.gen_objects_targets(object_test)

            # Run ninja
            try:
                result = subprocess.run(
                    ["ninja"], capture_output=True, text=True
                )

                expected_exit_code = object_test["expected_exit_code"]
                if result.returncode != expected_exit_code:
                    print(
                        " Test failed: Expected exit code"
                        f" {expected_exit_code}, got {result.returncode}"
                    )
                    print("Error:", result.stdout)
                    results.append(False)
                    continue

                if expected_exit_code != 0 and object_test.get(
                    "expected_error"
                ):
                    if not re.search(
                        object_test["expected_error"], result.stdout
                    ):
                        print(
                            " Test failed: Expected error message"
                            f" '{object_test['expected_error']}' not found"
                        )
                        print("Error output:", result.stdout)
                        results.append(False)
                        continue

                # Format generated files before comparing
                if expected_exit_code == 0:
                    test_dir = os.path.join(
                        self.objects_generated_dir, obj_name
                    )
                    if os.path.exists(test_dir):
                        print("    Formatting generated files...")
                        self.format_directory_files(test_dir)

                if (
                    expected_exit_code == 0
                    and "expected_output_files" in object_test
                ):
                    all_match = True
                    for group_name, expected_files in object_test[
                        "expected_output_files"
                    ].items():
                        for expected_file in expected_files:
                            generated_path = os.path.join(
                                self.objects_generated_dir,
                                obj_name,
                                group_name,
                                expected_file,
                            )
                            expected_path = os.path.join(
                                self.objects_expected_output_dir,
                                obj_name,
                                group_name,
                                expected_file,
                            )

                            try:
                                if not filecmp.cmp(
                                    generated_path,
                                    expected_path,
                                    shallow=False,
                                ):
                                    print(
                                        " Test failed: Generated file"
                                        f" {generated_path} doesn't match"
                                        " expected output"
                                    )
                                    all_match = False
                                    break
                            except FileNotFoundError as e:
                                print(f" Test failed: File not found - {e}")
                                all_match = False
                                break

                        if not all_match:
                            break

                    if all_match:
                        print(f" PASSED: {obj_name}")
                        results.append(True)
                    else:
                        results.append(False)
                else:
                    print(f" PASSED: {obj_name}")
                    results.append(True)

            except FileNotFoundError:
                print(
                    "Error: ninja command not found. Make sure it's installed"
                    " and in your PATH."
                )
                results.append(False)
        return results

    def clean_test_data(self):
        """Clean coverage data and generated files"""
        if os.path.exists(".ninja_log"):
            os.remove(".ninja_log")

        if os.path.exists(self.rules_file):
            os.remove(self.rules_file)

        if os.path.exists(".coverage"):
            os.remove(".coverage")

        for file in glob.glob(".coverage.*"):
            os.remove(file)

        dirs = [
            self.types_generated_dir,
            self.hypercalls_generated_dir,
            self.events_generated_dir,
            self.registers_generated_dir,
            self.objects_generated_dir,
            "htmlcov",
        ]
        for dir_path in dirs:
            if os.path.exists(dir_path):
                shutil.rmtree(dir_path)

    def run_tests(self):
        """Run all tests with coverage"""
        self.clean_test_data()

        cov = coverage.Coverage(config_file=".coveragerc")
        cov.start()

        print("=" * 80)
        print("CODEGEN TEST SUITE")
        print("=" * 80)

        types_results = self.test_types()
        hypercalls_results = self.test_hypercalls()
        events_results = self.test_events()
        registers_results = self.test_registers()
        objects_results = self.test_objects()

        all_results = (
            types_results
            + hypercalls_results
            + events_results
            + registers_results
            + objects_results
        )

        print("\n" + "=" * 80)
        print("TEST SUITE COMPLETE")
        print("=" * 80)

        # Print summary
        total_tests = len(all_results)
        passed_tests = sum(all_results)
        failed_tests = total_tests - passed_tests

        print(f"\nTotal tests run: {total_tests}")
        print(f"Passed: {passed_tests}")
        print(f"Failed: {failed_tests}")

        if failed_tests > 0:
            print(f"\nTest suite FAILED ({failed_tests} failures)")

        cov.stop()
        cov.save()

        return 0 if failed_tests == 0 else 1


if __name__ == "__main__":
    tester = CodegenTester()
    exit_code = tester.run_tests()
    exit(exit_code)
