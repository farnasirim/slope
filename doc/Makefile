.PHONY: all

all:
	pdflatex -halt-on-error thesis.tex
	bibtex thesis.aux
	pdflatex -halt-on-error thesis.tex
	bibtex thesis.aux
	pdflatex -halt-on-error thesis.tex
	bibtex thesis.aux


clean:
	$(RM) thesis.pdf *.aux *.bak *.bbl *.blg *.log *.out *.toc *.brf *.loa *.lot
