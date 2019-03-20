#define LOG_TAG "NXStreamManager"
#include <cutils/properties.h>
#include <cutils/str_parms.h>
#include <utils/Condition.h>
#include <utils/Mutex.h>
#include <utils/Thread.h>
#include <log/log.h>
#include <sys/mman.h>

#include <hardware/camera.h>
#ifdef ANDROID_PIE
#include <CameraMetadata.h>
#else
#include <camera/CameraMetadata.h>
#endif

#include <linux/videodev2.h>
#include <linux/media-bus-format.h>
#include <libnxjpeg.h>

#include "GlobalDef.h"
#include "metadata.h"
#include "StreamManager.h"

#define TIME_MSEC		1000000  /*1ms*/
#define THREAD_TIMEOUT		30000000 /*30ms*/

/*#define VERIFY_FRIST_FRAME*/
#ifdef VERIFY_FRIST_FRAME
buffer_handle_t firstFrame = NULL;
#endif

#ifdef ANDROID_PIE
using ::android::hardware::camera::common::V1_0::helper::CameraMetadata;
#endif
namespace android {

void StreamManager::setCaptureResult(uint32_t type, NXCamera3Buffer *buf)
{
	ALOGDV("[%s:%d] get result from %d frame, type:%d", __func__,
			mCameraId, buf->getFrameNumber(), type);

	if (type >= NX_MAX_STREAM)
		ALOGE("[%s:%d] Invalid type", __func__, mCameraId);
	else {
		mResultQ[type].queue(buf);
		mWakeUp.signal();
	}
}

void StreamManager::getCaptureResult(const struct nx_camera3_callback_ops *ops,
		uint32_t type,
		NXCamera3Buffer *buf)
{
	StreamManager *d = const_cast<StreamManager*>(static_cast<const StreamManager*>(ops->priv));
	d->setCaptureResult(type, buf);
}

int StreamManager::configureStreams(camera3_stream_configuration_t *stream_list)
{
	ALOGDD("[%s:%d] operation mode=%d", __func__, mCameraId,
			stream_list->operation_mode);

	for (uint32_t i = 0; i < stream_list->num_streams; i++) {
		camera3_stream_t *stream = stream_list->streams[i];
		ALOGDD("[%d:%zu] format:0x%x, width:%d, height:%d, usage:0x%x",
				mCameraId, i, stream->format, stream->width,
				stream->height, stream->usage);
		mStream[i] = new Stream(mCameraId, mFd[0], mScaler, mDeinterlacer,
				mAllocator, &mResultCb, stream, i, mInterlaced);
		if (mStream[i] == NULL) {
			ALOGE("[%s:%d] Failed to create stream:%d", __func__, mCameraId, i);
			return -EINVAL;
		}
		stream->priv = mStream[i]->getStream();
		ALOGDD("[%d: %zu] Mode:%d", mCameraId, i, mStream[i]->getMode());
	}

	return NO_ERROR;
}

int StreamManager::getRunningStreamsCount(void)
{
	int count = 0;

	for (int j = 0; j < NX_MAX_STREAM; j++)
	{
		if ((mStream[j] != NULL) && (mStream[j]->isRunning()) &&
				(!mStream[j]->getSkipFlag()))
				count++;
	}
	ALOGDV("[%s:%d] running streams count:%d", __func__, mCameraId, count);
	return count;
}

int StreamManager::runStreamThread(camera3_stream_t *s)
{
	int count = getRunningStreamsCount();
	Stream *stream = NULL;
	bool	skip = false;
	char	name[32] = {0, };

	stream = (Stream *)s->priv;
	if (stream == NULL) {
		ALOGE("[%s:%d] Failed to get stream from buffer", __func__, mCameraId);
		return -EINVAL;
	}
	ALOGDV("[%s:%d] getRunningStreamsCount:%d", __func__, mCameraId, count);

	if (!count) {
		stream->setSkipFlag(skip);
		stream->setHandle(mFd[count]);
		if (!skip) {
			if (stream->skipFrames())
				goto fail;
		}
		if (stream->prepareForRun() == NO_ERROR) {
			sprintf(name, "Stream[%d] Thread", stream->getMode());
			stream->run(name, PRIORITY_DEFAULT);
		} else
			goto fail;
	} else {
		for (int j = 0; j < NX_MAX_STREAM; j++)
		{
			if ((mStream[j] != NULL) && (mStream[j]->isRunning())) {
				if ((mStream[j]->getWidth() == s->width) &&
					(mStream[j]->getHeight() == s->height)) {
#ifdef VERIFY_FRIST_FRAME
					mStream[j]->stopStreaming();
#endif
				} else {
					mStream[j]->stopStreaming();
				}
			}
		}
		count = getRunningStreamsCount();
		ALOGDV("[%s:%d] get current RunningStreamsCount:%d", __func__,
				mCameraId, count);
		if (mFd[count] < 0)
			skip = true;
		else
			stream->setHandle(mFd[count]);
		stream->setSkipFlag(skip);
		if (!skip) {
			if (stream->skipFrames())
				goto fail;
		}
		if (stream->prepareForRun() == NO_ERROR) {
			sprintf(name, "Stream[%d] Thread", stream->getMode());
			stream->run(name, PRIORITY_DEFAULT);
		} else
			goto fail;
	}
	return 0;

fail:
	ALOGE("[%s:%d] Failed to run stream[%d] thread", __func__,
			mCameraId, stream->getMode());
	return -EINVAL;
}

int StreamManager::registerRequests(camera3_capture_request_t *r)
{
	CameraMetadata setting;
	camera_metadata_t *meta = NULL;
	sp<Stream> stream;
	int ret = NO_ERROR;

	if (r->settings != NULL) {
		setting = r->settings;
	} else {
		ALOGDV("[%s:%d] metadata is null", __func__, mCameraId);
		setting = mMeta;
	}

	ALOGDD("[%s:%d] frame number:%d, num_output_buffers:%d", __func__,
			mCameraId, r->frame_number, r->num_output_buffers);

	if (setting.exists(ANDROID_REQUEST_ID)) {
		ALOGDI("[%s:%d] requestID:%d", __func__, mCameraId,
				setting.find(ANDROID_REQUEST_ID).data.i32[0]);
		if (mPipeLineDepth == MAX_BUFFER_COUNT)
			mPipeLineDepth = 1;
		else
			mPipeLineDepth++;
	} else
		ALOGDI("[%s:%d] ===> no request id", __func__, mCameraId);
	meta = setting.release();
	if (r->input_buffer != NULL) {
		const camera3_stream_buffer_t *b = r->input_buffer;
		if ((b == NULL) || (b->status)) {
			ALOGE("[%s:%d] input buffer or status is not valid to use:%d",
					__func__, mCameraId, b->status);
			return -EINVAL;
		}
#ifdef TRACE_STREAM
		private_handle_t *ph = (private_handle_t *)*b->buffer;
		camera3_stream_t *s = b->stream;
		ALOGDD("[%s:%d] [Input] frmaeNumber:%d, format:0x%x, width:%d, height:%d, size:%d",
				__func__, mCameraId, r->frame_number, s->format, s->width,
				s->height, ph->size);
#endif
	}
	for (uint32_t i = 0; i < r->num_output_buffers; i++) {
		const camera3_stream_buffer_t *b = &r->output_buffers[i];

		if ((b == NULL) || (b->status)) {
			ALOGE("[%s:%d] buffer or status is not valid to use:%d",
					__func__, mCameraId, b->status);
			return -EINVAL;
		}

		private_handle_t *ph = (private_handle_t *)*b->buffer;
		if (ph->share_fd < 0) {
			ALOGE("[%s:%d] Invalid Buffer --> no fd", __func__, mCameraId);
			return -EINVAL;
		}
		ALOGDI("[%s:%d] [Output] frmaeNumber:%d, format:0x%x, width:%d, height:%d, size:%d",
				__func__, mCameraId, r->frame_number, ph->format,
				ph->width, ph->height, ph->size);
		Stream *s = (Stream *)(b->stream->priv);
		if (s == NULL) {
			ALOGE("[%s:%d] Failed to get stream for this buffer",
					__func__, mCameraId);
			return -EINVAL;
		}
		ret = s->registerBuffer(r->frame_number, b, meta);
		if (ret) {
			ALOGE("[%s:%d] Failed to register Buffer for buffer:%d",
					__func__, mCameraId, ret);
			return ret;
		}
		if (!s->isRunning()) {
			ret = runStreamThread(b->stream);
			if (ret)
				return ret;
		}
	}

	nx_camera_request_t *request = (nx_camera_request_t*)malloc(sizeof(nx_camera_request_t));
	if (!request) {
		ALOGE("[%s:%d] Failed to malloc for request", __func__, mCameraId);
		ret = -ENOMEM;
	}
	request->frame_number = r->frame_number;
	request->num_output_buffers = r->num_output_buffers;
	request->meta = meta;
	request->input_buffer = r->input_buffer;
	mRequestQ.queue(request);
	mMeta = meta;
	if (!isRunning()) {
		ALOGDV("[%s:%d] START StreamManager Thread", __func__,
				mCameraId);
		run("StreamManagerThread", PRIORITY_DEFAULT);
	}
	return ret;
}

int StreamManager::stopStream()
{
	int ret = NO_ERROR, i;

	ALOGDD("[%s:%d] Enter", __func__, mCameraId);
	for (i = 0; i < NX_MAX_STREAM; i++) {
		if ((mStream[i] != NULL) && (mStream[i]->isRunning()))
			mStream[i]->stopStreaming();
	}
	ALOGDV("[%s:%d] mRequestQ:%d, mRQ:%d, mResultQ[%d:%d:%d:%d]",
			__func__, mCameraId, mRequestQ.size(), mRQ.size(), mResultQ[0].size(),
			mResultQ[1].size(), mResultQ[2].size(), mResultQ[3].size());

	while (!mRequestQ.isEmpty() || !mRQ.isEmpty()) {
		ALOGDV("[%s:%d] Wait buffer drained", __func__, mCameraId);
		usleep(1000);
	}

	for (i = 0; i < NX_MAX_STREAM; i++) {
		if (mStream[i] != NULL) {
			mStream[i].clear();
			mStream[i] = NULL;
		}
	}
#ifdef VERIFY_FRIST_FRAME
	if (mAllocator && firstFrame) {
		mAllocator->free(mAllocator, firstFrame);
		firstFrame = NULL;
	}
#endif
	ALOGDD("[%s:%d]", __func__, mCameraId);
	if (isRunning()) {
		mWakeUp.signal();
		ALOGDV("[%s:%d] requestExitAndWait Enter", __func__, mCameraId);
		requestExitAndWait();
		ALOGDV("[%s:%d] requestExitAndWait Exit", __func__, mCameraId);
	}
	return ret;
}

private_handle_t* StreamManager::getSimilarActiveStream(camera3_stream_buffer_t *out,
		int num_buffers, camera3_stream_t *s)
{
	private_handle_t *buf = NULL;
	for (int i = 0; i < num_buffers; i++) {
		camera3_stream_t *st = out[i].stream;
		Stream *stream = (Stream*)st->priv;
		if ((stream != NULL) && (stream->getFormat() != HAL_PIXEL_FORMAT_BLOB) &&
				(!stream->getSkipFlag()) &&
				(stream->getWidth() == s->width) &&
				(stream->getHeight() == s->height)) {
			buf = (private_handle_t*)*out[i].buffer;
			break;
		}
	}

	return buf;
}

int StreamManager::sendResult(void)
{
	nx_camera_request_t *request = mRequestQ.getHead();
	if (!request) {
		ALOGE("[%s:%d] Failed to get request from Queue",
				__func__, mCameraId);
		return -EINVAL;
	}

	/* notify */
	nsecs_t timestamp = systemTime(SYSTEM_TIME_MONOTONIC);
	/*ALOGDD("[Recorded file duration] [SENSOR] camera_id:%d - :%ld", mCameraId,
			nanoseconds_to_milliseconds(systemTime(SYSTEM_TIME_MONOTONIC)));*/
	camera3_notify_msg_t msg;
	memset(&msg, 0x0, sizeof(camera3_notify_msg_t));
	msg.type = CAMERA3_MSG_SHUTTER;
	msg.message.shutter.frame_number = request->frame_number;
	msg.message.shutter.timestamp = timestamp;
	mCb->notify(mCb, &msg);

	/* send result */
	private_handle_t *preview = NULL;
	camera3_capture_result_t result;
	exif_attribute_t *exif = NULL;
	bzero(&result, sizeof(camera3_capture_result_t));
	result.frame_number = request->frame_number;
	result.num_output_buffers = request->num_output_buffers;

	camera3_stream_buffer_t output_buffers[result.num_output_buffers];
	for (uint32_t i = 0; i < result.num_output_buffers; i++) {
		Stream *stream = NULL;
		NXCamera3Buffer *buf = mRQ.dequeue();
		if (buf) {
			output_buffers[i].stream = buf->getStream();
			output_buffers[i].buffer = buf->getBuffer();
			output_buffers[i].release_fence = -1;
			output_buffers[i].acquire_fence = -1;
			output_buffers[i].status = 0;
		} else {
			ALOGE("[%s:%d] Failed to get buffer form RQ", __func__, mCameraId);
			break;
		}

		stream = (Stream *)buf->getStream()->priv;
		if (stream != NULL) {
			if (!stream->getSkipFlag() &&
					(stream->getFormat() != HAL_PIXEL_FORMAT_BLOB)) {
				preview =
					(private_handle_t *)buf->getPrivateHandle();
			}
			if ((stream->getFormat() == HAL_PIXEL_FORMAT_BLOB) &&
					(exif == NULL)) {
					exif = new exif_attribute_t();
					if (!exif)
						ALOGE("[%s:%d] Failed to make exif",
								__func__, mCameraId);
			}
		} else
			ALOGDV("[%s:%d] setream is null\n", __func__, mCameraId);
	}
#ifdef VERIFY_FRIST_FRAME
	if (result.frame_number == 2) {
		private_handle_t *buffer = (private_handle_t *)*output_buffers[0].buffer;
		if (firstFrame == NULL) {
			stream->allocBuffer(buffer->width, buffer->height,
					buffer->format, &firstFrame);
			if (firstFrame == NULL)
				ALOGE("[%s:%d] Failed to alloc a new buffer",
						__func__, mCameraId);
		}
		if (firstFrame) {
			ALOGDD("[%s:%d] ========> save first frame", __func__, mCameraId);
			stream->scaling((private_handle_t*)firstFrame, buffer, request->meta);
		}
	}
	if (result.num_output_buffers == 2 && firstFrame) {
		private_handle_t *buffer = NULL;
		camera3_stream_t *s = NULL;
		ALOGDD("[%s:%d] =========> copy first frame", __func__, mCameraId);
		s = output_buffers[0].stream;
		if (s->usage & GRALLOC_USAGE_HW_TEXTURE)
			buffer = (private_handle_t *)*output_buffers[1].buffer;
		else
			buffer = (private_handle_t *)*output_buffers[0].buffer;
		stream->scaling(buffer, (private_handle_t*)firstFrame, request->meta);
		if (mAllocator && firstFrame) {
			mAllocator->free(mAllocator, firstFrame);
			firstFrame = NULL;
		}
	}
#endif
	camera_metadata_t *meta = translateMetadata(mCameraId, request->meta, exif,
			timestamp, mPipeLineDepth);
	result.result = (const camera_metadata_t *)meta;
	if (preview != NULL) {
		for (uint32_t i = 0; i < result.num_output_buffers; i++) {
			camera3_stream_t *s = output_buffers[i].stream;
			Stream *stream = (Stream *)s->priv;
			private_handle_t *buffer = (private_handle_t *)*output_buffers[i].buffer;
			if (stream->getSkipFlag()) {
				private_handle_t *copy = getSimilarActiveStream(output_buffers,
						result.num_output_buffers, s);
				if (copy == NULL)
					copy = preview;
				if (stream->getFormat() == HAL_PIXEL_FORMAT_BLOB) {
					if ((stream->getWidth() != (uint32_t)copy->width) ||
						(stream->getHeight() != (uint32_t)copy->height)) {
						ALOGE("[%s:%d] Resolution is different", __func__,
								mCameraId);
					} else {
						ALOGD("[%s:%d] jpegEncoding", __func__, mCameraId);
						uint32_t crop[4] = {0, 0};
						if (stream->calScalingFactor(request->meta, crop))
							exif->setCropResolution(crop[0], crop[1], crop[2], crop[3]);
						else
							exif->setCropResolution(0, 0, buffer->width, buffer->height);
						stream->jpegEncoding(buffer, copy, exif);
					}
				} else
					stream->scaling(buffer, copy, meta);
			}
		}
	}
	result.output_buffers = output_buffers;
	result.partial_result = 1;
	result.input_buffer = request->input_buffer;
	ALOGDI("[%s:%d] frame_number:%d, num_output_buffers:%d", __func__,
			mCameraId, result.frame_number, result.num_output_buffers);
	mCb->process_capture_result(mCb, &result);
	request = mRequestQ.dequeue();
	if (request) {
		if (request->meta)
			free(request->meta);
		free(request);
	}
	ALOGDV("[%s:%d] Exit", __func__, mCameraId);
	free(meta);
	return 0;
}

void StreamManager::drainBuffer()
{
	ALOGDV("[%s:%d] Enter", __func__, mCameraId);

	while (!mRequestQ.isEmpty())
		sendResult();

	ALOGDV("[%s:%d] Exit", __func__, mCameraId);
}

status_t StreamManager::readyToRun()
{
	int ret = NO_ERROR;

	ALOGDV("[%s:%d]", __func__, mCameraId);
	return ret;
}

bool StreamManager::threadLoop()
{
	if (!mRequestQ.size()) {
		ALOGE("[%s:%d] mRequestQ size is '0'",
				__func__, mCameraId);
		return true;
	}
	nx_camera_request_t *request = mRequestQ.getHead();
	if (!request) {
		ALOGE("[%s:%d] Failed to get request from Queue",
			__func__, mCameraId);
		return true;
	}

	uint32_t frame_number = request->frame_number;
	uint32_t num_buffers = request->num_output_buffers;
	if (mNumBuffers)
		num_buffers = mNumBuffers;

	if (exitPending())
		return false;

	if(!mResultQ[0].size() && !mResultQ[1].size() && !mResultQ[2].size()) {
		int ret;

		ALOGDV("[%s:%d] wait", __func__, mCameraId);
		ret = mWakeUp.waitRelative(mLock, THREAD_TIMEOUT);
		if (ret == TIMED_OUT)
			ALOGE("[%s:%d] Wait Timeout:%dms", __func__,
					mCameraId,
					THREAD_TIMEOUT/TIME_MSEC);
		ALOGDV("[%s:%d] release", __func__, mCameraId);
	}

	for (int i = 0; i < NX_MAX_STREAM; i++) {
		int size = mResultQ[i].size();
		if (size > 0) {
			NXCamera3Buffer *buf = mResultQ[i].getHead();
			if ((buf) && (buf->getFrameNumber() == frame_number)) {
				ALOGDV("[%s:%d] got a buffer for the frame_buffer:%d from %d",
						__func__, mCameraId, frame_number, i);
				buf = mResultQ[i].dequeue();
				mRQ.queue(buf);
				num_buffers--;
				ALOGDV("[%s:%d] left buffers:%d", __func__, mCameraId,
						num_buffers);
				if (num_buffers == 0) {
					ALOGDV("[%s:%d] got all:%d buffers", __func__, mCameraId,
							request->num_output_buffers);
					sendResult();
					mNumBuffers = num_buffers;
					break;
				} else
					mNumBuffers = num_buffers;
			}
		}
	}
	return true;
}

}; /* namespace android */
