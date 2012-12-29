"
" Vim syntax file for the Chimp programming language.
" Maintainer: Tom Lee
" Latest Revision: 14 October 2012
"

if exists("b:current_syntax")
    finish
endif

syn keyword chimpKeyword nil use ret var and or panic fn while not _
syn keyword chimpKeyword receive match class this break
syn keyword chimpBoolean true false
syn keyword chimpConditional if else
syn keyword chimpType str int hash object array
syn keyword chimpSpecial init recv self spawn
syn match chimpString '"[^"]*"'
syn match chimpComment '#.*$'
syn match chimpInt '[0-9][0-9]*'
syn match chimpFunction '^[a-zA-Z_][a-zA-Z0-9_]*'

let b:current_syntax = "chimp"

hi def link chimpKeyword Keyword
hi def link chimpConditional Conditional
hi def link chimpType Type
hi def link chimpString String
hi def link chimpComment Comment
hi def link chimpFunction Function
hi def link chimpBoolean Boolean
hi def link chimpInt Number
hi def link chimpSpecial Identifier

