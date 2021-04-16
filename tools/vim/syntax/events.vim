" Vim syntax file
" Language:	Gunyah Event Files
" Extensions:   *.ev
"
" © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
"
" SPDX-License-Identifier: BSD-3-Clause

" quit when a syntax file was already loaded
"if exists("b:current_syntax")
"  finish
"endif

syn match evComment '\/\/.*$'

syn match moduleName contained /[_A-Za-z0-9]\+/
syn keyword module module interface contained nextgroup=moduleName skipwhite
syn match moduleDef '^\(module\|interface\)\>.*$' contains=module,evComment

syn match handlerName contained /[_A-Za-z0-9]\+/
syn keyword evHandler handler contained skipwhite nextgroup=handlerName
syn keyword evUnwinder unwinder contained skipwhite nextgroup=handlerName

syn match priorityNum contained /[0-9]\+\>/
syn keyword priorityKeyword contained first last
syn match priorityVal /[_A-Za-z0-9]\+/ contained contains=priorityNum,priorityKeyword
syn keyword priority priority contained skipwhite nextgroup=priorityVal

syn match evName contained /[_A-Za-z0-9]\+/
syn keyword evDirective subscribe event selector_event handled_event contained skipwhite nextgroup=evName

syn match evSubscriber '^subscribe\s.*$' skipnl nextgroup=evSubscriberBody contains=evComment,evDirective
syn match evSubscriberBody '^\(\s\+\).*$' contained skipnl nextgroup=evSubscriberBody contains=evComment,evHandler,evUnwinder,priority

" simple for now
"syn match evSep contained /:/ skipwhite nextgroup=evParam
syn match evParam contained /[_A-Za-z0-9]\+/ nextgroup=evSep
syn keyword evEventDef selector param return contained skipwhite nextgroup=evParam,evSep

syn match evEvent '^\(\(selector\|handled\)_\)\?event\s.*$' skipnl nextgroup=evEventBody contains=evComment,evDirective
syn match evEventBody '^\(\s\+\).*$' contained skipnl nextgroup=evEventBody contains=evComment,evEventDef

" TODO
" handler function params

hi def link evComment		Comment
hi def link evName 		Label
hi def link evDirective		Type
hi def link handlerName		Identifier
hi def link evParam		Identifier
hi def link evHandler		PreProc
hi def link evUnwinder		PreProc
hi def link evEventDef		PreProc
hi def link module		PreProc
hi def link priorityNum		Constant
hi def link priorityKeyword	Special
hi def link priority		PreProc
hi def link moduleName		Label

"hi def link evEventBody		TODO

let b:current_syntax = "events"
