" © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
"
" SPDX-License-Identifier: BSD-3-Clause
"
" This simple script, placed in VIMDIR/plugin/ will setup cscope for gunyah.

function! CScopeSetup()
   if filereadable("tools/misc/update_cscope.sh")
       let output=system('./tools/misc/update_cscope.sh')
       cscope reset
   endif
endfunction

call CScopeSetup()
