package org.mozilla.geckoview;

import android.support.annotation.Nullable;
import android.support.annotation.UiThread;

import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.util.BundleEventListener;
import org.mozilla.gecko.util.EventCallback;
import org.mozilla.gecko.util.GeckoBundle;

public class WebExtensionController implements BundleEventListener {
    public interface TabDelegate {
        /**
         * Called when tabs.create is invoked, this method returns a *newly-created* session
         * that GeckoView will use to load the requested page on. If the returned value
         * is null the page will not be opened.
         *
         * @param source An instance of {@link WebExtension} or null if extension was not registered
         *               with GeckoRuntime.registerWebextension
         */
        @UiThread
        @Nullable
        default GeckoResult<GeckoSession> onNewTab(@Nullable WebExtension source, @Nullable String uri) {
            return null;
        }
    }

    private GeckoRuntime mRuntime;
    private WebExtensionEventDispatcher mDispatcher;
    private TabDelegate mTabDelegate;

    protected WebExtensionController(final GeckoRuntime runtime, final WebExtensionEventDispatcher dispatcher) {
        mRuntime = runtime;
        mDispatcher = dispatcher;
    }

    @UiThread
    public void setTabDelegate(final @Nullable TabDelegate delegate) {
        if (delegate == null) {
            if (mTabDelegate != null) {
                EventDispatcher.getInstance().unregisterUiThreadListener(
                        this,
                        "GeckoView:WebExtension:NewTab"
                );
            }
        } else {
            if (mTabDelegate == null) {
                EventDispatcher.getInstance().registerUiThreadListener(
                        this,
                        "GeckoView:WebExtension:NewTab"
                );
            }
        }
        mTabDelegate = delegate;
    }

    @UiThread
    public @Nullable TabDelegate getTabDelegate() {
        return mTabDelegate;
    }

    private void newTab(final GeckoBundle message, final EventCallback callback) {
        if (mTabDelegate == null) {
            callback.sendSuccess(null);
            return;
        }

        WebExtension extension = mDispatcher.getWebExtension(message.getString("extensionId"));

        final GeckoResult<GeckoSession> result = mTabDelegate.onNewTab(extension, message.getString("uri"));

        if (result == null) {
            callback.sendSuccess(null);
            return;
        }

        result.accept(session -> {
            if (session == null) {
                callback.sendSuccess(null);
                return;
            }

            if (session.isOpen()) {
                throw new IllegalArgumentException("Must use an unopened GeckoSession instance");
            }

            session.open(mRuntime);

            callback.sendSuccess(session.getId());
        });
    }

    @Override
    public void handleMessage(final String event, final GeckoBundle message,
                              final EventCallback callback) {
        if ("GeckoView:WebExtension:NewTab".equals(event)) {
            newTab(message, callback);
            return;
        }
    }
}
