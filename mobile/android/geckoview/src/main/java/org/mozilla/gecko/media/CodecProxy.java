/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.media;

import android.media.MediaCodec;
import android.media.MediaCodec.BufferInfo;
import android.media.MediaCodec.CryptoInfo;
import android.media.MediaFormat;
import android.os.DeadObjectException;
import android.os.RemoteException;
import android.util.Log;

import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.gfx.GeckoSurface;
import org.mozilla.gecko.mozglue.JNIObject;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.Map;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;

// Proxy class of ICodec binder.
public final class CodecProxy {
    private static final String LOGTAG = "GeckoRemoteCodecProxy";
    private static final boolean DEBUG = false;
    @WrapForJNI private static final long INVALID_SESSION = -1;

    private ICodec mRemote;
    private long mSession;
    private boolean mIsEncoder;
    private FormatParam mFormat;
    private GeckoSurface mOutputSurface;
    private CallbacksForwarder mCallbacks;
    private String mRemoteDrmStubId;
    private Queue<Sample> mSurfaceOutputs = new ConcurrentLinkedQueue<>();
    private boolean mFlushed = true;

    private Map<Integer, SampleBuffer> mInputBuffers = new HashMap<>();
    private Map<Integer, SampleBuffer> mOutputBuffers = new HashMap<>();

    public interface Callbacks {
        void onInputStatus(long timestamp, boolean processed);
        void onOutputFormatChanged(MediaFormat format);
        void onOutput(Sample output, SampleBuffer buffer);
        void onError(boolean fatal);
    }

    @WrapForJNI
    public static class NativeCallbacks extends JNIObject implements Callbacks {
        public native void onInputStatus(long timestamp, boolean processed);
        public native void onOutputFormatChanged(MediaFormat format);
        public native void onOutput(Sample output, SampleBuffer buffer);
        public native void onError(boolean fatal);

        @Override // JNIObject
        protected void disposeNative() {
            throw new UnsupportedOperationException();
        }
    }

    private class CallbacksForwarder extends ICodecCallbacks.Stub {
        private final Callbacks mCallbacks;
        private boolean mCodecProxyReleased;

        CallbacksForwarder(final Callbacks callbacks) {
            mCallbacks = callbacks;
        }

        @Override
        public synchronized void onInputQueued(final long timestamp) throws RemoteException {
            if (!mCodecProxyReleased) {
                mCallbacks.onInputStatus(timestamp, true /* processed */);
            }
        }

        @Override
        public synchronized void onInputPending(final long timestamp) throws RemoteException {
            if (!mCodecProxyReleased) {
                mCallbacks.onInputStatus(timestamp, false /* processed */);
            }
        }

        @Override
        public synchronized void onOutputFormatChanged(final FormatParam format)
                throws RemoteException {
            if (!mCodecProxyReleased) {
                mCallbacks.onOutputFormatChanged(format.asFormat());
            }
        }

        @Override
        public synchronized void onOutput(final Sample sample) throws RemoteException {
            if (mCodecProxyReleased) {
                sample.dispose();
                return;
            }

            SampleBuffer buffer = CodecProxy.this.getOutputBuffer(sample.bufferId);
            if (mOutputSurface != null) {
                // Don't render to surface just yet. Callback will make that happen when it's time.
                mSurfaceOutputs.offer(sample);
            } else if (buffer == null) {
                // Buffer with given ID has been flushed.
                sample.dispose();
                return;
            }
            mCallbacks.onOutput(sample, buffer);
        }

        @Override
        public void onError(final boolean fatal) throws RemoteException {
            reportError(fatal);
        }

        private synchronized void reportError(final boolean fatal) {
            if (!mCodecProxyReleased) {
                mCallbacks.onError(fatal);
            }
        }

        private synchronized void setCodecProxyReleased() {
            mCodecProxyReleased = true;
        }
    }

    @WrapForJNI
    public static CodecProxy create(final boolean isEncoder,
                                    final MediaFormat format,
                                    final GeckoSurface surface,
                                    final Callbacks callbacks,
                                    final String drmStubId) {
        return RemoteManager.getInstance().createCodec(isEncoder, format, surface, callbacks, drmStubId);
    }

    public static CodecProxy createCodecProxy(final boolean isEncoder,
                                              final MediaFormat format,
                                              final GeckoSurface surface,
                                              final Callbacks callbacks,
                                              final String drmStubId) {
        return new CodecProxy(isEncoder, format, surface, callbacks, drmStubId);
    }

    private CodecProxy(final boolean isEncoder, final MediaFormat format,
                       final GeckoSurface surface, final Callbacks callbacks,
                       final String drmStubId) {
        mIsEncoder = isEncoder;
        mFormat = new FormatParam(format);
        mOutputSurface = surface;
        mRemoteDrmStubId = drmStubId;
        mCallbacks = new CallbacksForwarder(callbacks);
    }

    boolean init(final ICodec remote) {
        try {
            remote.setCallbacks(mCallbacks);
            if (!remote.configure(mFormat, mOutputSurface, mIsEncoder ? MediaCodec.CONFIGURE_FLAG_ENCODE : 0, mRemoteDrmStubId)) {
                return false;
            }
            remote.start();
        } catch (RemoteException e) {
            e.printStackTrace();
            return false;
        }

        mRemote = remote;
        return true;
    }

    boolean deinit() {
        try {
            mRemote.stop();
            mRemote.release();
            mRemote = null;
            return true;
        } catch (RemoteException e) {
            e.printStackTrace();
            return false;
        }
    }

    @WrapForJNI
    public synchronized boolean isAdaptivePlaybackSupported() {
        if (mRemote == null) {
            Log.e(LOGTAG, "cannot check isAdaptivePlaybackSupported with an ended codec");
            return false;
        }
        try {
            return mRemote.isAdaptivePlaybackSupported();
        } catch (RemoteException e) {
            e.printStackTrace();
            return false;
        }
    }

    @WrapForJNI
    public synchronized boolean isHardwareAccelerated() {
        if (mRemote == null) {
            Log.e(LOGTAG, "cannot check isHardwareAccelerated with an ended codec");
            return false;
        }
        try {
            return mRemote.isHardwareAccelerated();
        } catch (RemoteException e) {
            e.printStackTrace();
            return false;
        }
    }

    @WrapForJNI
    public synchronized boolean isTunneledPlaybackSupported() {
        if (mRemote == null) {
            Log.e(LOGTAG, "cannot check isTunneledPlaybackSupported with an ended codec");
            return false;
        }
        try {
            return mRemote.isTunneledPlaybackSupported();
        } catch (RemoteException e) {
            e.printStackTrace();
            return false;
        }
    }

    @WrapForJNI
    public synchronized long input(final ByteBuffer bytes, final BufferInfo info,
                                      final CryptoInfo cryptoInfo) {
        if (mRemote == null) {
            Log.e(LOGTAG, "cannot send input to an ended codec");
            return INVALID_SESSION;
        }

        boolean eos = info.flags == MediaCodec.BUFFER_FLAG_END_OF_STREAM;

        if (eos) {
            return sendInput(Sample.EOS);
        }

        try {
            Sample s = mRemote.dequeueInput(info.size);
            fillInputBuffer(s.bufferId, bytes, info.offset, info.size);
            mSession = s.session;
            return sendInput(s.set(info, cryptoInfo));
        } catch (RemoteException | NullPointerException e) {
            Log.e(LOGTAG, "fail to dequeue input buffer", e);
        } catch (IOException e) {
            Log.e(LOGTAG, "fail to copy input data.", e);
            // Balance dequeue/queue.
            sendInput(null);
        }
        return INVALID_SESSION;
    }

    private void fillInputBuffer(final int bufferId, final ByteBuffer bytes,
            final int offset, final int size) throws RemoteException, IOException {
        if (bytes == null || size == 0) {
            Log.w(LOGTAG, "empty input");
            return;
        }

        SampleBuffer buffer = mInputBuffers.get(bufferId);
        if (buffer == null) {
            buffer = mRemote.getInputBuffer(bufferId);
            mInputBuffers.put(bufferId, buffer);
        }
        buffer.readFromByteBuffer(bytes, offset, size);
    }

    private long sendInput(final Sample sample) {
        try {
            mRemote.queueInput(sample);
            if (sample != null) {
                sample.dispose();
                mFlushed = false;
            }
        } catch (Exception e) {
            Log.e(LOGTAG, "fail to queue input:" + sample, e);
            return INVALID_SESSION;
        }
        return mSession;
    }

    @WrapForJNI
    public synchronized boolean flush() {
        if (mFlushed) {
            return true;
        }
        if (mRemote == null) {
            Log.e(LOGTAG, "cannot flush an ended codec");
            return false;
        }
        try {
            if (DEBUG) {
                Log.d(LOGTAG, "flush " + this);
            }
            resetBuffers();
            mRemote.flush();
            mFlushed = true;
        } catch (DeadObjectException e) {
            return false;
        } catch (RemoteException e) {
            e.printStackTrace();
            return false;
        }
        return true;
    }

    private void resetBuffers() {
        for (SampleBuffer b : mInputBuffers.values()) {
            b.dispose();
        }
        mInputBuffers.clear();
        for (SampleBuffer b : mOutputBuffers.values()) {
            b.dispose();
        }
        mOutputBuffers.clear();
    }

    @WrapForJNI
    public boolean release() {
        mCallbacks.setCodecProxyReleased();
        synchronized (this) {
            if (mRemote == null) {
                Log.w(LOGTAG, "codec already ended");
                return true;
            }
            if (DEBUG) {
                Log.d(LOGTAG, "release " + this);
            }

            if (!mSurfaceOutputs.isEmpty()) {
                // Flushing output buffers to surface may cause some frames to be skipped and
                // should not happen unless caller release codec before processing all buffers.
                Log.w(LOGTAG, "release codec when " + mSurfaceOutputs.size() + " output buffers unhandled");
                try {
                    for (Sample s : mSurfaceOutputs) {
                        mRemote.releaseOutput(s, true);
                    }
                } catch (RemoteException e) {
                    e.printStackTrace();
                }
                mSurfaceOutputs.clear();
            }

            resetBuffers();

            try {
                RemoteManager.getInstance().releaseCodec(this);
            } catch (DeadObjectException e) {
                return false;
            } catch (RemoteException e) {
                e.printStackTrace();
                return false;
            }
            return true;
        }
    }

    @WrapForJNI
    public synchronized boolean setRates(final int newBitRate) {
        if (!mIsEncoder) {
            Log.w(LOGTAG, "this api is encoder-only");
            return false;
        }

        if (android.os.Build.VERSION.SDK_INT < 19) {
            Log.w(LOGTAG, "this api was added in API level 19");
            return false;
        }

        if (mRemote == null) {
            Log.w(LOGTAG, "codec already ended");
            return true;
        }

        try {
            mRemote.setRates(newBitRate);
        } catch (RemoteException e) {
            Log.e(LOGTAG, "remote fail to set rates:" + newBitRate);
            e.printStackTrace();
        }
        return true;
    }

    @WrapForJNI
    public synchronized boolean releaseOutput(final Sample sample, final boolean render) {
        if (mOutputSurface != null) {
            if (!mSurfaceOutputs.remove(sample)) {
                if (mRemote != null) Log.w(LOGTAG, "already released: " + sample);
                return true;
            }

            if (DEBUG && !render) {
                Log.d(LOGTAG, "drop output:" + sample.info.presentationTimeUs);
            }
        }

        if (mRemote == null) {
            Log.w(LOGTAG, "codec already ended");
            sample.dispose();
            return true;
        }

        try {
            mRemote.releaseOutput(sample, render);
        } catch (RemoteException e) {
            Log.e(LOGTAG, "remote fail to release output:" + sample.info.presentationTimeUs);
            e.printStackTrace();
        }
        sample.dispose();

        return true;
    }

    /* package */ void reportError(final boolean fatal) {
        mCallbacks.reportError(fatal);
    }

    private synchronized SampleBuffer getOutputBuffer(final int id) {
        if (mRemote == null) {
            Log.e(LOGTAG, "cannot get buffer#" + id + " from an ended codec");
            return null;
        }

        if (mOutputSurface != null || id == Sample.NO_BUFFER) {
            return null;
        }

        SampleBuffer buffer = mOutputBuffers.get(id);
        if (buffer != null) {
            return buffer;
        }

        try {
            buffer = mRemote.getOutputBuffer(id);
        } catch (Exception e) {
            Log.e(LOGTAG, "cannot get buffer#" + id, e);
            return null;
        }
        if (buffer != null) {
            mOutputBuffers.put(id, buffer);
        }

        return buffer;
    }
}
