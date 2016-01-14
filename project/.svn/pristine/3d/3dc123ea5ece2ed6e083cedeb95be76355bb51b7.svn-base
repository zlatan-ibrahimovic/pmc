TEX = latexmk -pdf

all: rapport.pdf


rapport.pdf: rapport.tex title.tex
	$(TEX) rapport.tex
	okular rapport.pdf &

.PHONY: clean

clean:
	rm -f *.aux *.log *.toc *.pdf *.bbl *.blg *.out *~  *.pdf *.f* 
