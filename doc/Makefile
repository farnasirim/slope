.PHONY: all bench-readonly.pgf

bench-readonly.pgf:
	cd .. && \
	./analyze.py --dir=bench-results --tag=readonly-chunked-10000 plot-readonly --texname=$@ && mv $@ doc

run:
	touch thesis.pdf
	while true ; do make all && (diff thesis.pdf view.pdf || mv thesis.pdf view.pdf) ; sleep 2 ; done

all: bench-readonly.pgf
	pdflatex -enable-write18 -halt-on-error -interaction=nonstopmode thesis.tex
	bibtex thesis.aux
	pdflatex -enable-write18 -halt-on-error -interaction=nonstopmode thesis.tex
	pdflatex -enable-write18 -halt-on-error -interaction=nonstopmode thesis.tex


clean:
	$(RM) thesis.pdf *.aux *.bak *.bbl *.blg *.log *.out *.toc *.brf *.loa *.lot
