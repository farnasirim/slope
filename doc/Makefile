.PHONY: all

plotdeps: ../analyze.py ../readonly.py ../writeall.py

bench-readonly.pgf: plotdeps
	cd .. && \
	./analyze.py plot --dir=bench-results --tag=readonly-chunked7-10000 --workload=readonly --texname=$@ && mv $@ doc

bench-writeall.pgf: plotdeps
	cd .. && \
	./analyze.py plot --dir=bench-results --tag=writeall-chunked7-10000 --workload=writeall --texname=$@ && mv $@ doc

run:
	touch thesis.pdf
	while true ; do make all && (diff thesis.pdf view.pdf || mv thesis.pdf view.pdf) ; sleep 2 ; done

all: bench-readonly.pgf bench-writeall.pgf
	pdflatex -enable-write18 -halt-on-error -interaction=nonstopmode thesis.tex
	bibtex thesis.aux
	pdflatex -enable-write18 -halt-on-error -interaction=nonstopmode thesis.tex
	pdflatex -enable-write18 -halt-on-error -interaction=nonstopmode thesis.tex


clean:
	$(RM) thesis.pdf *.aux *.bak *.bbl *.blg *.log *.out *.toc *.brf *.loa *.lot *.pgf
