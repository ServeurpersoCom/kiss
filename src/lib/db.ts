import type { Conversation } from './types.js';
import { DB_NAME, DB_STORE, DB_VERSION } from './config.js';

// one IndexedDB connection for the whole page, opened on first use and
// opened again after a failure
let dbPromise: Promise<IDBDatabase> | null = null;

function open(): Promise<IDBDatabase> {
	if (dbPromise) return dbPromise;
	dbPromise = new Promise((resolve, reject) => {
		const req = indexedDB.open(DB_NAME, DB_VERSION);
		req.onupgradeneeded = () => {
			if (!req.result.objectStoreNames.contains(DB_STORE)) {
				req.result.createObjectStore(DB_STORE, { keyPath: 'id' });
			}
		};
		req.onsuccess = () => resolve(req.result);
		req.onerror = () => {
			dbPromise = null;
			reject(req.error);
		};
	});
	return dbPromise;
}

// one request in its own transaction, settled once the transaction commits
function tx<T>(mode: IDBTransactionMode, fn: (store: IDBObjectStore) => IDBRequest<T>): Promise<T> {
	return open().then(
		(db) =>
			new Promise((resolve, reject) => {
				const t = db.transaction(DB_STORE, mode);
				const req = fn(t.objectStore(DB_STORE));
				t.oncomplete = () => resolve(req.result);
				t.onerror = t.onabort = () => reject(t.error ?? req.error);
			})
	);
}

// newest first
export async function listConversations(): Promise<Conversation[]> {
	const all = await tx<Conversation[]>('readonly', (s) => s.getAll());
	return all.sort((a, b) => b.updated - a.updated);
}

export function putConversation(conversation: Conversation): Promise<IDBValidKey> {
	return tx('readwrite', (s) => s.put(conversation));
}

export function deleteConversation(id: string): Promise<undefined> {
	return tx('readwrite', (s) => s.delete(id));
}
