import { useEffect } from "react";

/**
 * Run a lightweight status refresh only while the WebView is visible.
 *
 * Android may keep a WebView alive after its manager screen is backgrounded.
 * Continuing shell-backed status queries in that state wastes wakeups and
 * process launches without producing visible information. Refresh once when
 * the page becomes visible again so the UI catches up immediately.
 */
export function useVisibleInterval(
	callback: () => void,
	intervalMs: number,
	enabled = true,
): void {
	useEffect(() => {
		if (!enabled) return;

		let timer: number | undefined;

		const stop = (): void => {
			if (timer !== undefined) {
				window.clearInterval(timer);
				timer = undefined;
			}
		};

		const start = (): void => {
			stop();
			if (document.visibilityState !== "visible") return;
			timer = window.setInterval(callback, intervalMs);
		};

		const onVisibilityChange = (): void => {
			if (document.visibilityState === "visible") {
				callback();
				start();
			} else {
				stop();
			}
		};

		start();
		document.addEventListener("visibilitychange", onVisibilityChange);
		return () => {
			stop();
			document.removeEventListener("visibilitychange", onVisibilityChange);
		};
	}, [callback, intervalMs, enabled]);
}
