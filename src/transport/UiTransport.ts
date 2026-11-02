// ==============================================================================
// Lucid Engine - Dual-Transport UI Shell Contract
// ==============================================================================

export type ConnectionState = 'connecting' | 'connected' | 'reconnecting' | 'closed';

export interface UiTransport {
  readonly mode: 'native-ipc' | 'remote-ws';

  /**
   * Subscribes to the continuous binary telemetry stream (60-120 Hz).
   * Callback receives a zero-copy DataView over the consistent 40-byte snapshot.
   * Returns an unsubscribe function.
   */
  subscribeState(onSnapshot: (view: DataView) => void): () => void;

  /**
   * Dispatches a binary RPC command to the C++ engine over the MPSC queue.
   */
  sendCommand(commandId: number, payload?: ArrayBufferView): Promise<void>;

  /**
   * Subscribes to transport connection lifecycle changes.
   */
  onConnectionChange(listener: (state: ConnectionState) => void): () => void;

  /**
   * Gracefully closes and disposes the transport adapter.
   */
  dispose(): void;
}
