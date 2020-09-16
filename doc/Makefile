.PHONY: bench-readonly.pgf bench-writeall.pgf bench-readonly-hp.pgf bench-writeall-hp.pgf bench-bloomfilter.pgf  bench-bloomfilter-hp.pgf bench-map-hp.pgf bench-map.pgf all

plotdeps: ../analyze.py ../readonly.py ../writeall.py

bench-readonly.pgf: plotdeps
	cd .. && \
	./analyze.py plot --dir=bench-results --tag=readonly-10000-1s2235 --workload=readonly --texname=$@ && mv $@ doc

bench-readonly-hp.pgf: plotdeps
	cd .. && \
	./analyze.py plot --dir=bench-results --tag=readonly-15s1223 --workload=readonly --texname=$@ && mv $@ doc

bench-writeall.pgf: plotdeps
	cd .. && \
	./analyze.py plot --dir=bench-results --tag=writeall-10000-1s2235 --workload=writeall --texname=$@ && mv $@ doc

bench-writeall-hp.pgf: plotdeps
	cd .. && \
	./analyze.py plot --dir=bench-results --tag=writeall-15s1244 --workload=writeall --texname=$@ && mv $@ doc

bench-bloomfilter.pgf: plotdeps
	cd .. && \
	./analyze.py plot --dir=bench-results --tag=bloomfilter-16s0159 --workload=bloomfilter --texname=$@ && mv $@ doc

bench-bloomfilter-hp.pgf: plotdeps
	cd .. && \
	./analyze.py plot --dir=bench-results --tag=bloomfilter-16s0202 --workload=bloomfilter --texname=$@ && mv $@ doc

bench-map.pgf: plotdeps
	cd .. && \
	./analyze.py plot --dir=bench-results --tag=map-16s0221 --workload=map --texname=$@ && mv $@ doc

bench-map-hp.pgf: plotdeps
	cd .. && \
	./analyze.py plot --dir=bench-results --tag=map-16s0222 --workload=map --texname=$@ && mv $@ doc



run:
	touch thesis.pdf
	while true ; do make all && (diff thesis.pdf view.pdf || mv thesis.pdf view.pdf) ; sleep 2 ; done

all: bench-readonly.pgf bench-writeall.pgf bench-readonly-hp.pgf  bench-writeall-hp.pgf  bench-bloomfilter.pgf  bench-bloomfilter-hp.pgf bench-map-hp.pgf bench-map.pgf
	pdflatex -enable-write18 -halt-on-error -interaction=nonstopmode thesis.tex
	bibtex thesis.aux
	pdflatex -enable-write18 -halt-on-error -interaction=nonstopmode thesis.tex
	pdflatex -enable-write18 -halt-on-error -interaction=nonstopmode thesis.tex


clean:
	$(RM) thesis.pdf *.aux *.bak *.bbl *.blg *.log *.out *.toc *.brf *.loa *.lot *.pgf
