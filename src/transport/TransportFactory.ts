// ==============================================================================
// Lucid Engine - Dual-Transport Factory
// Automatically selects Native IPC or Remote WebSocket based on runtime environment
// ==============================================================================

import type { UiTransport } from './UiTransport';
import { NativeIpcTransport } from './NativeIpcTransport';
import { RemoteWsTransport } from './RemoteWsTransport';

export function createUiTransport(forceMode?: 'native-ipc' | 'remote-ws'): UiTransport {
  if (forceMode === 'native-ipc') {
    return new NativeIpcTransport();
  }
  if (forceMode === 'remote-ws') {
    return new RemoteWsTransport();
  }

  // Automatic environment detection:
  // 1. WebView2 Native Bridge
  const isWebView2 = typeof (window as any).chrome?.webview !== 'undefined';
  // 2. Intercepted memory domain
  const isInternalDomain = typeof window !== 'undefined' && window.location?.hostname === 'lucid.internal';

  if (isWebView2 || isInternalDomain) {
    console.log('[UiTransport] Selected: NativeIpcTransport (Shared Memory)');
    return new NativeIpcTransport();
  }

  console.log('[UiTransport] Selected: RemoteWsTransport (Single-Port WebSocket)');
  return new RemoteWsTransport();
}
