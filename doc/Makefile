all: thesis.pdf

run:
	while sleep .2; do yes x | make -s t.pdf; done

t.pdf: thesis.pdf
	mv $< $@
	cp $@ $<

show: thesis.pdf
	xdg-open $< &

%.aux %.pdf: %.tex
	pdflatex $*

%.bbl: %.aux
	bibtex $*

thesis.pdf: ${wildcard *.tex} thesis.bbl

thesis.bbl: thesis.bib

clean:
	$(RM) thesis.pdf *.aux *.bak *.bbl *.blg *.log *.out *.toc *.brf *.loa *.lot
