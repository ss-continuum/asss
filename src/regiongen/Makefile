
SPDIR=../../scripts
SRCDIR=..

regiongen: regiongen.c $(SRCDIR)/region.c $(SRCDIR)/util.c sparse.inc
	gcc -DNOTHREAD -I$(SRCDIR) -Wall -O2 -o $@ regiongen.c $(SRCDIR)/region.c $(SRCDIR)/util.c

sparse.inc: $(SPDIR)/gensparse.py $(SPDIR)/regiongen_params.py
	$(SPDIR)/gensparse.py $@ $(SPDIR)/regiongen_params.py

clean:
	rm -f regiongen *.o *.rgn sparse.inc

