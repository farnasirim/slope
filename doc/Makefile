.PHONY: all

run:
	while true ; do make all && (diff thesis.pdf view.pdf || mv thesis.pdf view.pdf) ; sleep 2 ; done

all:
	pdflatex -enable-write18 -halt-on-error -interaction=nonstopmode thesis.tex
	bibtex thesis.aux
	pdflatex -enable-write18 -halt-on-error -interaction=nonstopmode thesis.tex
	bibtex thesis.aux
	pdflatex -enable-write18 -halt-on-error -interaction=nonstopmode thesis.tex
	bibtex thesis.aux


clean:
	$(RM) thesis.pdf *.aux *.bak *.bbl *.blg *.log *.out *.toc *.brf *.loa *.lot
