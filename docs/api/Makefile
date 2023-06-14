%.pdf: %.md Makefile
	pandoc -s --toc --pdf-engine=xelatex -N --top-level-division=part \
		--metadata=title:'Gunyah Hypercall API' \
		--variable=class:book \
		--variable=mainfont:LiberationSans \
		--variable=monofont:LiberationMono \
		$< -o $@

all: gunyah_api.pdf
