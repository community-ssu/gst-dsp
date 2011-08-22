/*
 * Copyright (C) 2009-2010 Felipe Contreras
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * Authors:
 * Felipe Contreras <felipe.contreras@gmail.com>
 * Juha Alanen <juha.m.alanen@nokia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "dsp_bridge.h"
#include "dmm_buffer.h"

#include "gstdspbase.h"
#include "gstdspvenc.h"

/**
 * HD-H264Enc_create_args:
 *
 * @size: size of this structure
 * @num_streams : number of streams used to communicate with the Socket Node.
 * Typically two output streams, one input and one output.
 * @width: Encoder processing width of the frame. It can be less than or equal to
 * max width.
 * @height: Encoder processing height of the frame. It can be less than or equal
 * to max Height.
 * @max_width: Encoder will be created for this width of the frame.
 * @max_height: Encoder will be created for this height of the frame.
 * @gob_length: This is effectively the I-frame interval. The number of  frames
 * between I frames.
 * @search_range: The maximum length of the motion vector components (both x and y)
 * (only 8 or 16 is supported)
 * @constrained_intrapred_flag: 1/0 that enable/disable constrained Intra prediction.
 * @num_unit_intick: number of time units of a clock operating at the frequency
 * time_scale Hz that corresponds to one increment (called a clock tick) of a
 * clock tick counter. This is used only for VUI.
 * @timescale: frame rate * num_unit_intick. This is used only for VUI.
 * @bitrate: Target bitrate in bits per second.
 * @rc_mode: Rate Control Mode
 *     - 0: Constant Bitrate
 *     - 3: Variable Bitrate
 * @framerate: Encoding frames per second.
 * @profile: H264 profile_id as defined in [1]. Only Baseline profile supported (66)
 * @level: level_id as defined in [1]
 * @color_format: YUV sample format:
 *     - YUV 422i (fourcc: UYVY) : 4
 *     - YUV 420p (fourcc: I420) : 1
 * @stream_format:
 *     - 0 : Bytestream format accroding to annex B of [1]:
 *            Encoder produces SPS,PPS and normal NAL units in one single buffer.
 *            It has NAL start code in front of each NALU
 *     - 1 : NAL unit and its size separately:
 *            Encoder outputs one buffer for all NAL units and each NAL unit
 *            won't be preceded by the size of the NAL unit or the start code.
 *            Size of each NAL is specified in the array "outargs.bytesgenerated"
 *            (array[0] holds No. of NAL units and array[1] onwards holds each
 *            NAL unit size).
 *     - 2 : NALU(2byte):
 *            Encoder outputs one buffer for all NAL units in a frame and each
 *            NAL unit precede by the size of the NAL unit, using 2 bytes for the length
 *     - 3 : NALU(4byte):
 *            Encoder outputs one buffer for all NAL units and each NAL unit
 *            preceded by the size of the NAL unit, using 4 bytes for the length
 * @cir_mbs_n: Number of Macroblocks in the kernel used in Cyclic Intra Refresh
 * @intra4x4modeflag: Boolean flag that control the usage of this feature.
 * @max_delay: Maximum delay allowed in Constant BitRate mode. The delay models
 *             the buffering that happens in the trasnmission channel.
 *             300ms and 1000ms are the Min and Max value supported for max_delay
 *
 * @no_of_mbrows_per_slice: Number of MB rows per slice (e.g for QCIF max value
 * can be 9). 0 in case where 1 frame is contained in a NALU
 * @disable_dblock_across_slice: 1/0 that disables/enables deblocking across slice.
 * @hqp_interval: Number of frames between Golden P frames
 * @hqp_alloc_increase_percentage: percentage increase in bits allocation for
 * High Quality P frames as compared to the nonHQP frames, which indirectly
 * reduces the QP for HQP frames. 30% is the max value allowed
 *
 *
 * This strucure is used to initialize the socket node (SN) that encapsulates
 * the H264 HD encoder algorithm.
 *
 *
 * References:
 * [1] H.264 Standard:  http://www.itu.int/rec/T-REC-H.264-201003-I/
 */

struct create_args {
	uint32_t size;
	uint16_t num_streams;

	uint16_t in_id;
	uint16_t in_type;
	uint16_t in_count;

	uint16_t out_id;
	uint16_t out_type;
	uint16_t out_count;

	uint32_t width;
	uint32_t height;
	uint32_t max_width;
	uint32_t max_height;
	uint32_t gob_length;
	uint32_t search_range;
	uint32_t constrained_intrapred_flag;
	int32_t  num_unit_intick;
	int32_t  timescale;
	uint32_t bitrate;
	int32_t  rc_mode;
	int32_t  framerate;
	uint32_t profile;
	uint32_t level;
	uint8_t color_format;
	int32_t stream_format;
	uint32_t cir_mbs_n;
	int32_t intra4x4modeflag;
	int32_t max_delay;
	uint32_t no_of_mbrows_per_slice;
	uint32_t disable_dblock_across_slice;
	int32_t  hqp_interval;
	int32_t  hqp_alloc_increase_percentage;
};

static void create_args(GstDspBase *base, unsigned *profile_id, void **arg_data)
{
	GstDspVEnc *self = GST_DSP_VENC(base);
	unsigned mbpf = self->width / 16 * self->height / 16;
	unsigned frame_interval = self->keyframe_interval * self->framerate;

	struct create_args args = {
		.size = sizeof(args) - 4,
		.num_streams = 2,
		.in_id = 0,
		.in_type = 0,
		.in_count = base->ports[0]->num_buffers,
		.out_id = 1,
		.out_type = 0,
		.out_count = base->ports[1]->num_buffers,

		.width = self->width,
		.height = self->height,
		.max_width = self->width,
		.max_height = self->height,
		.search_range = 8,
		.num_unit_intick = 1,
		.timescale = self->framerate,
		.bitrate = self->bitrate,
		.framerate = self->framerate,
		.profile = 66, /* Baseline profile */
		.level = self->level,
		.color_format = (self->color_format == GST_MAKE_FOURCC('U', 'Y', 'V', 'Y') ? 4 : 1),
		.stream_format = self->priv.h264.bytestream ? 0 : 3,
		.intra4x4modeflag = 1, /* 0-disable 1- enable */
		.max_delay = 300, /* max allowed delay */
		.rc_mode = self->mode == 0 ? 3 : 0,
	};

	/* Support for frame rotation in CBR mode for resolutions till WVGA */
	if (self->mode == 1 && ((self->width * self->height) >> 8) <= 1590)
		args.max_width = args.max_height = (self->width > self->height) ? self->width : self->height;

	/* MBs/Sec for WVGA @ 30Fps --> 47700 */
	if ((((self->width * self->height) >> 8) * self->framerate) > 47700)
		args.intra4x4modeflag = 0;
	else
		/* To improve quality for resolutions till WVGA */
		args.search_range = 24;

	/* Setting GOB length according to keyframe interval property */
	if (self->keyframe_interval) {
		args.gob_length = self->gob_length = self->keyframe_interval * self->framerate;
		if (args.gob_length > 150) {
			args.gob_length = self->gob_length = 150;
			self->keyframe_interval = args.gob_length / self->framerate;
		}
	} else {
		/* For infinite GOB */
		args.gob_length = self->gob_length = 255;

		if (self->mode != 1) {
			pr_err(base, "invalid keyframe interval");
			*arg_data = NULL;
			return;
		}
	}

	if (self->intra_refresh)
		args.cir_mbs_n = frame_interval ? mbpf / frame_interval : 1;

	if (self->priv.h264.slice_size_mb &&
	   self->priv.h264.slice_size_mb < mbpf)
	{
		unsigned mb_per_row = self->width / 16;
		args.no_of_mbrows_per_slice = self->priv.h264.slice_size_mb / mb_per_row;
		args.disable_dblock_across_slice = 1;
	}

	switch (self->level) {
	case 9:
	case 10:
		if (self->width/16 > 28 || self->height/16 > 28)
			args.level = 11;
		break;
	case 11:
	case 12:
	case 13:
	case 20:
		if (self->width/16 > 56 || self->height/16 > 56)
			args.level = 21;
		break;
	case 21:
		if (self->width/16 > 79 || self->height/16 > 79)
			args.level = 22;
		break;
	case 22:
	case 30:
		if (self->width/16 > 113 || self->height/16 > 113)
			args.level = 31;
		break;
	case 31:
		if (self->width/16 > 169 || self->height/16 > 169)
			args.level = -1;
		break;
	}

	switch (args.level) {
	case 9:
		base->output_buffer_size = 350;
		break;
	case 10:
		base->output_buffer_size = 175;
		break;
	case 11:
		base->output_buffer_size = 500;
		break;
	case 12:
		base->output_buffer_size = 1000;
		break;
	case 13:
	case 20:
		base->output_buffer_size = 2000;
		break;
	case 21:
	case 22:
		base->output_buffer_size = 4000;
		break;
	case 30:
		base->output_buffer_size = 10000;
		break;
	case 31:
		base->output_buffer_size = 14000;
		break;
	default:
		base->output_buffer_size = 0;
		break;
	}

	base->output_buffer_size = ((base->output_buffer_size * 1200 * 3) >> 2) >> 3;

	*profile_id = 0;

	*arg_data = malloc(sizeof(args));
	memcpy(*arg_data, &args, sizeof(args));
}

struct in_params {
	uint32_t frame_index;
	uint32_t generate_header;
	uint32_t bitrate;
	uint8_t  force_i_frame;
	uint32_t width;
	uint32_t height;
	uint32_t gob_length;
	int32_t  framerate;
	uint32_t start_mb;
	uint32_t num_of_mbs;
};

struct out_params {
	int32_t frame_type;
	int32_t ext_error_code;
	uint32_t nalus_per_frame;
	uint32_t nalu_sizes[240];
	uint8_t skip_frame;
};

static void in_send_cb(GstDspBase *base, struct td_buffer *tb)
{
	struct in_params *param;
	GstDspVEnc *self = GST_DSP_VENC(base);
	param = tb->params->data;
	param->frame_index      = g_atomic_int_exchange_and_add(&self->frame_index, 1);
	param->bitrate          = g_atomic_int_get(&self->bitrate);
	param->width            = g_atomic_int_get(&self->width);
	param->height           = g_atomic_int_get(&self->height);
	param->gob_length       = g_atomic_int_get(&self->gob_length);
	param->framerate        = g_atomic_int_get(&self->framerate);
	param->start_mb         = 1;
	param->num_of_mbs       = 0;

	g_mutex_lock(self->keyframe_mutex);
	param->force_i_frame = self->keyframe_event ? 1 : 0;
	if (self->keyframe_event) {
		gst_pad_push_event(base->srcpad, self->keyframe_event);
		self->keyframe_event = NULL;
	}
	g_mutex_unlock(self->keyframe_mutex);
}

static void create_codec_data(GstDspBase *base)
{
	GstDspVEnc *self = GST_DSP_VENC(base);
	guint8 *sps, *pps, *codec_data;
	guint16 sps_size, pps_size, offset;

	sps = GST_BUFFER_DATA(self->priv.h264.sps);
	pps = GST_BUFFER_DATA(self->priv.h264.pps);

	sps_size = GST_BUFFER_SIZE(self->priv.h264.sps);
	pps_size = GST_BUFFER_SIZE(self->priv.h264.pps);

	offset = 0;

	self->priv.h264.codec_data = gst_buffer_new_and_alloc(sps_size + pps_size + 11);
	codec_data = GST_BUFFER_DATA(self->priv.h264.codec_data);

	codec_data[offset++] = 0x01;
	codec_data[offset++] = sps[1]; /* AVCProfileIndication*/
	codec_data[offset++] = sps[2]; /* profile_compatibility*/
	codec_data[offset++] = sps[3]; /* AVCLevelIndication */
	codec_data[offset++] = 0xff;
	codec_data[offset++] = 0xe1;
	codec_data[offset++] = (sps_size >> 8) & 0xff;
	codec_data[offset++] = sps_size & 0xff;

	memcpy(codec_data + offset, sps, sps_size);
	offset += sps_size;

	codec_data[offset++] = 0x1;
	codec_data[offset++] = (pps_size >> 8) & 0xff;
	codec_data[offset++] = pps_size & 0xff;

	memcpy(codec_data + offset, pps, pps_size);
}

static void ignore_sps_pps_header(GstDspBase *base, dmm_buffer_t *b)
{
	char *data = b->data;

	if ((data[4] & 0x1f) == 7 || (data[4] & 0x1f) == 8)
		base->skip_hack_2++;
}

static void out_recv_cb(GstDspBase *base, struct td_buffer *tb)
{
	GstDspVEnc *self = GST_DSP_VENC(base);
	dmm_buffer_t *b = tb->data;
	struct out_params *param;
	param = tb->params->data;


	if (XDM_ERROR_IS_FATAL(param->ext_error_code)) {
		pr_err(base, "invalid i/p params or insufficient o/p buf size");
		g_atomic_int_set(&base->status, GST_FLOW_ERROR);
	}

	pr_debug(self, "frame type %d", param->frame_type);
	tb->keyframe = (param->frame_type != 1);

	if (G_UNLIKELY(param->skip_frame))
		b->skip = TRUE;

	if (b->len == 0)
		return;

	if (self->priv.h264.bytestream)
		return;

	if (G_LIKELY(self->priv.h264.codec_data_done)) {
		/* prefix the NALU with a lenght field, not counting the start code */
		*(uint32_t *)b->data = GINT_TO_BE(b->len - 4);
		ignore_sps_pps_header(base, b);
	}
	else {
		if (!self->priv.h264.sps_received) {
			/* skip the start code 0x00000001 when storing SPS */
			self->priv.h264.sps = gst_buffer_new_and_alloc(b->len - 4);
			memcpy(GST_BUFFER_DATA(self->priv.h264.sps), b->data + 4, b->len - 4);
			self->priv.h264.sps_received = TRUE;
		} else if (!self->priv.h264.pps_received) {
			/* skip the start code 0x00000001 when storing PPS */
			self->priv.h264.pps = gst_buffer_new_and_alloc(b->len - 4);
			memcpy(GST_BUFFER_DATA(self->priv.h264.pps), b->data + 4, b->len - 4);
			self->priv.h264.pps_received = TRUE;
		}

		if (self->priv.h264.pps_received && self->priv.h264.sps_received) {
			create_codec_data(base);
			if (gstdsp_set_codec_data_caps(base, self->priv.h264.codec_data)) {
				self->priv.h264.codec_data_done = TRUE;
				gst_buffer_replace(&self->priv.h264.sps, NULL);
				gst_buffer_replace(&self->priv.h264.pps, NULL);
				gst_buffer_replace(&self->priv.h264.codec_data, NULL);
			}
		}
		base->skip_hack_2++;
	}
}

static void setup_in_params(GstDspBase *base,  dmm_buffer_t *tmp)
{
	struct in_params *in_param;
	GstDspVEnc *self = GST_DSP_VENC(base);

	in_param = tmp->data;
	in_param->frame_index   = 0;
	in_param->force_i_frame = 0;
	in_param->bitrate       = self->bitrate;
	in_param->width         = self->width;
	in_param->height        = self->height;
	in_param->gob_length    = self->gob_length;
	in_param->framerate     = self->framerate;
	in_param->start_mb      = 1;
	in_param->num_of_mbs    = 0;
	in_param->generate_header = self->priv.h264.bytestream ? 0 : 2; /* 0- 1st frame with sps+pps+frame,
									   1- 1st-sps+pps and 2nd- frame,
									   2- 1st-sps, 2nd-pps and 3rd- frame data */
}

static void setup_params(GstDspBase *base)
{
	struct in_params *in_param;
	struct out_params *out_param;
	du_port_t *p;

	p = base->ports[0];
	gstdsp_port_setup_params(base, p, sizeof(*in_param), setup_in_params);
	p->send_cb = in_send_cb;

	p = base->ports[1];
	gstdsp_port_setup_params(base, p, sizeof(*out_param), NULL);
	p->recv_cb = out_recv_cb;
}

struct td_codec td_hdh264enc_codec = {
	.uuid = &(const struct dsp_uuid) { 0x268E9368, 0x032A, 0x4DE6, 0x93, 0xDD,
		{ 0x8C, 0x3D, 0x0A, 0x5A, 0xEE, 0x0B } },
	.filename = "h264bpenc_sn.dll64P",
	.setup_params = setup_params,
	.create_args = create_args,
};
