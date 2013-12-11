/*
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses>.
 *
 */

#define VDEST NAME "-1080p30.mp4 "

#define SSDEST "/pipeline-examples/media/snapshot-" NAME "-1080p30-%d.jpg "

#define SOURCE "videotestsrc is-live=true name=eospad ! capsfilter caps=video/x-raw,width=640,height=480 ! "\
  "perf print-arm-load=true ! tee name=tsrc "

#define SNAPSHOT "tsrc. ! queue  ! omxscaler ! capsfilter caps=video/x-raw,width=1280,height=720 name=filter ! jpegenc name=snapshot ! multifilesink location=" SSDEST "async=false "
//~ #define SNAPSHOT "tsrc. ! queue !  omxscaler ! fakesink "
#define ENCODER "tsrc. ! queue ! omxh264enc ! tee name=tenc "

#ifdef NOREC
# define RECORD "tenc. ! fakesink"
#else
# define RECORD "tenc. ! queue ! h264parse ! qtmux name=mux ! filesink location=/tmp/" VDEST "async=false "
#endif

#define PIPE SOURCE SNAPSHOT ENCODER RECORD
