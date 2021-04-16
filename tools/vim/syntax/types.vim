" Vim syntax file
" Language:	Gunyah Type Files
" Extensions:   *.tc
"
" © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
"
" SPDX-License-Identifier: BSD-3-Clause

" quit when a syntax file was already loaded
"if exists("b:current_syntax")
"  finish
"endif

syn match tcComment '\/\/.*$' contains=@Spell

syn match Constant contained '\<\(0x[0-9a-fA-f]\+\|0b[01]\+\|[0-9]\+\)\>'
syn match Bits contained '<[0-9]\+>'
syn match TypeName contained /[_A-Za-z0-9]\+/

syn keyword tcBaseType contained sint8 sint16 sint32 sint64 bool uint8 uint16 uint32 uint64 char sintptr uintptr sregister uregister size
syn keyword tcQual contained pointer array atomic aligned
syn keyword tcBitmap contained unknown

syn match tcContained contained "\%#=1\<contained\>"

syn match tcConstantEqual /=/ contained skipwhite nextgroup=Constant
syn keyword tcConstant constant contained skipwhite nextgroup=tcConstantEqual
syn keyword Aggregates type object public structure enumeration bitfield contained
syn keyword newtype newtype
syn region tcNewType start="newtype" end=";" contained contains=tcBaseType,tcQual,Constant,Aggregates,newtype
syn keyword tcDefine define contained skipwhite nextgroup=TypeName
syn keyword tcExtend extend contained skipwhite nextgroup=TypeName

"syn match assignment '=' skipwhite contained nextgroup=Constant
"hi def link assignment 	TODO

syn region tcDef start="\(define\|extend\)" end=";" keepend contains=tcDefine,tcExtend,Bits,Aggregates,tcConstant,tcNewType,tcQual,tcBlock,tcComment

"hi def link tcDef 		Error

syn region tcBlock start="{" end="}" contained extend contains=Constant,tcComment,tcBaseType,Aggregates,tcQual,tcBitmap,cppDirective,cppSingle,tcContained
"hi def link tcBlock 		TODO

syntax region cppDirective	display start="^\s*\zs\(%:\|#\)\s*\(define\|if\|ifdef\|elif\|error\)\>\s\+" skip="\\$" end="$" keepend contains=Constant
syntax match cppSingle	display /^\s*\zs\(%:\|#\)\s*\(endif\|else\)\>/

hi def link tcComment		Comment
hi def link tcDefine 		Identifier
hi def link tcExtend 		tcDefine
hi def link cppDirective	PreProc
hi def link cppSingle 		cppDirective
hi def link Aggregates 		Keyword
hi def link newtype		Keyword
hi def link tcQual 		Keyword
hi def link tcBitmap		Special
hi def link tcConstant 		Keyword
hi def link tcContained		Keyword
"hi def link TypeName		Define
hi def link tcBaseType		Type
hi def link Bits		Operator

let b:current_syntax = "types"
