

Ok, there's way too little information here at the moment. I wasn't
expecting other people to work on anything for a while, so many things
could use cleaning up and elaborating.

There's a quick guide to writing modules in doc/tutorial.txt. I don't
bother describing any of the interfaces in detail, I leave that job to
the header files, which should describe their own interfaces. But they
don't do that very well. Sorry.

As for balls, they work something like this: the server has a setting
telling it how many balls there are. It sends out a ball position packet
for each ball to everyone in the arena (use net->SendToArena). It does
this periodically (I think it's every 10 seconds, not sure) and whenever
someone picks up or fires a ball. They're sent unreliably (use
NET_UNRELIABLE). The ball module should process two packets:
C2S_SHOOTBALL and C2S_PICKUPBALL. It should send S2C_BALL packets. Goals
will be later, because I suspect they might involve simulating physics,
and that's... let's just say nontrivial.

Again, I'd be extremely surprised if you manage to write anything based
on the tiny amount of information I've written so far. Ask me questions,
please.

--Grelminar

