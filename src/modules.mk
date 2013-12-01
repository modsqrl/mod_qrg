mod_qrg.la: mod_qrg.slo
	$(SH_LINK) -rpath $(libexecdir) -module -avoid-version  mod_qrg.lo
DISTCLEAN_TARGETS = modules.mk
shared =  mod_qrg.la
