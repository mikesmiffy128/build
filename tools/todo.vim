function! Todo(...)
	if exists("a:1") && a:1 != ""
		copen
		" cex "" | cadde to avoid insta-jumping
		cex "" | cadde system("git grep -nF \"TODO(\"".shellescape(a:1)."\\)")
	else
		" just displaying like this for now...
		cex "" | cclose | !tools/todo
	endif
endfunction

function! TodoEdit(...)
	if exists("a:1") && a:1 != ""
		exec "tabe TODO/".a:1
		if line('$') == 1 && getline(1) == ''
			normal o====
			1
		else
			3 " XXX should really search for ====, but this is fine for now
		endif
	else
		echoerr "Specify an issue ID"
	endif
endfunction

command! -nargs=? Todo call Todo("<args>")
command! -nargs=? TodoEdit call TodoEdit("<args>")

" vi: sw=4 ts=4 noet tw=80 cc=80
