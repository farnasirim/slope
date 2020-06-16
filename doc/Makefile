.PHONY: all

all:
	pdflatex thesis.tex
	bibtex thesis.aux
	pdflatex thesis.tex
	bibtex thesis.aux
	pdflatex thesis.tex
	bibtex thesis.aux


clean:
	$(RM) thesis.pdf *.aux *.bak *.bbl *.blg *.log *.out *.toc *.brf *.loa *.lot
