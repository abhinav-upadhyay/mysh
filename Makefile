.include <bsd.own.mk>

MAN.sh=		# none

PROGS=			sh
SRCS=	sh.c
CFLAGS+=	-g -O0

LDFLAGS+=	-L.

LDADD+= -lutil -lm -lcurses -lspell

BINDIR=		/usr/bin


.include <bsd.prog.mk>
