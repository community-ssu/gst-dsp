/*
 * Copyright (C) 2010 Nokia Corporation
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef TD_HCODEC_H
#define TD_HCODEC_H

#include "gstdspbase.h"

static inline void
handle_hdcodec_error(GstDspBase *base,
		int error_code,
		int ext_error_code)
{
	if (error_code == -1 && XDM_ERROR_IS_UNSUPPORTED(ext_error_code)) {
		pr_err(base, "unsupported stream or invalid i/p params");
		g_atomic_int_set(&base->status, GST_FLOW_ERROR);
	}
	else if (XDM_ERROR_IS_APPLIEDCONCEALMENT(ext_error_code))
		pr_debug(base, "corrupted input stream");
}

#endif
