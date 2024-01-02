%.pdf: %.md Makefile
	pandoc -s --toc --pdf-engine=xelatex -N --top-level-division=part \
		--metadata=title:'Gunyah Hypercall API' \
		--metadata=date:"Generated: `date \"+%a %d %B %Y\"`" \
		--variable=class:book \
		--variable=mainfont:LiberationSans \
		--variable=monofont:LiberationMono \
		--variable=papersize:a4  \
		--variable=margin-left:2.5cm  \
		--variable=margin-right:2.5cm  \
		--variable=margin-top:2.5cm  \
		--variable=margin-bottom:2.5cm  \
		$< -o $@

all: gunyah_api.pdf gunyah_api_qcom.pdf
