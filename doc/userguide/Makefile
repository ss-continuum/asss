
### Global definitions

ASSSDIR = ../..

SHELL = /bin/sh
LATEX = latex
DVIPS = dvips
XDVI = xdvi

### Primary targets

DVITARGETS = userguide.dvi
PSTARGETS = userguide.ps
PDFTARGETS = userguide.pdf
HTMLTARGETS = userguide.html
TEXTARGETS = commands.tex settings.tex
ALLTARGETS = $(DVITARGETS) $(PSTARGETS) $(PDFTARGETS) $(HTMLTARGETS) $(TEXTARGETS)

### Dummy targets

default: view

dvi: $(DVITARGETS)

ps: $(PSTARGETS)

pdf: $(PDFTARGETS)

html: $(HTMLTARGETS)

all: dvi ps pdf html

view: dvi
	( for a in $(DVITARGETS) ; do $(XDVI) $$a & done ) > /dev/null

clean:
	$(RM) -f $(ALLTARGETS) *.log *.aux *.blg *.bak *.hlog *-Z-* *~

publish: pdf html
	scp *.pdf *.html *.gif ellie:userguide


### Dependencies and commands

commands.tex: $(ASSSDIR)/src/*.c $(ASSSDIR)/scripts/extract-docs.py
	cat $(ASSSDIR)/src/*.c | $(ASSSDIR)/scripts/extract-docs.py > $@

settings.tex: $(ASSSDIR)/src/*.c $(ASSSDIR)/scripts/extract-cfg-docs.py
	cat $(ASSSDIR)/src/*.{c,def} | $(ASSSDIR)/scripts/extract-cfg-docs.py -l > $@

userguide.dvi: userguide.tex commands.tex settings.tex
	latex userguide.tex

userguide.ps: userguide.dvi
	$(DVIPS) $(TWOSIDEDSTUFF) -o userguide.ps userguide.dvi

userguide.pdf: userguide.tex commands.tex
	pdflatex userguide.tex

userguide.html: userguide.tex commands.tex
	tex2page userguide.tex

