import WebSocket from 'ws';

/**
 * Shared RPC client utility for Lucid Engine test suites.
 * Handles single-port dual transport (safely filtering binary frames),
 * incremental monotonic IDs, and timeout guarantees.
 */
export class TestRpcClient {
  constructor(url = 'ws://127.0.0.1:9000') {
    this.url = url;
    this.ws = null;
    this.idCounter = 0;
    this.pending = new Map();
  }

  connect(timeoutMs = 5000) {
    return new Promise((resolve, reject) => {
      this.ws = new WebSocket(this.url);

      const timer = setTimeout(() => {
        reject(new Error(`Timeout connecting to ${this.url}`));
      }, timeoutMs);

      this.ws.on('open', () => {
        clearTimeout(timer);
        resolve();
      });

      this.ws.on('error', (err) => {
        clearTimeout(timer);
        reject(err);
      });

      this.ws.on('message', (data) => {
        // Filter out binary telemetry packets
        if (typeof data !== 'string' && !(data instanceof String)) {
          // If Buffer, check if it starts with valid JSON '{'
          if (Buffer.isBuffer(data) && data.length > 0 && data[0] !== 0x7B) {
            return; // Skip binary telemetry frame
          }
        }

        try {
          const text = data.toString();
          if (!text.startsWith('{')) return;
          const msg = JSON.parse(text);

          if (msg.id !== undefined && this.pending.has(msg.id)) {
            const { resolve, reject, timer } = this.pending.get(msg.id);
            clearTimeout(timer);
            this.pending.delete(msg.id);

            if (msg.error) {
              reject(new Error(msg.error.message || JSON.stringify(msg.error)));
            } else {
              resolve(msg.result);
            }
          }
        } catch {
          // Ignore binary/malformed messages
        }
      });

      this.ws.on('close', () => {
        for (const [, { reject, timer }] of this.pending) {
          clearTimeout(timer);
          reject(new Error('WebSocket connection closed'));
        }
        this.pending.clear();
      });
    });
  }

  send(method, params = {}, timeoutMs = 10000) {
    return new Promise((resolve, reject) => {
      if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
        return reject(new Error('WebSocket not connected'));
      }

      const id = ++this.idCounter;
      const timer = setTimeout(() => {
        if (this.pending.has(id)) {
          this.pending.delete(id);
          reject(new Error(`RPC Timeout for method: ${method} (id=${id})`));
        }
      }, timeoutMs);

      this.pending.set(id, { resolve, reject, timer });
      try {
        this.ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
      } catch (err) {
        clearTimeout(timer);
        this.pending.delete(id);
        reject(err);
      }
    });
  }

  close() {
    if (this.ws) {
      try {
        this.ws.close();
      } catch {}
      this.ws = null;
    }
  }
}
