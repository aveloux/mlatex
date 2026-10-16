/**
 * Loads the engine: latex.wasm, and latex.data with the fonts, from beside
 * latex.js unless `locateFile` says where they are.
 */
declare function Latex(options?: Latex.Options): Promise<Latex.Engine>;

declare namespace Latex {
	interface Options {
		/** The address of latex.wasm or latex.data, given its name and latex.js's folder. */
		locateFile?: (path: string, prefix: string) => string;
	}

	interface Engine {
		Session: new () => Session;
	}

	/** The values, commands and files every document it makes is handed. */
	interface Session {
		/** A value the document reads with `\variable{name}`. */
		set(name: string, value: string): void;
		unset(name: string): void;
		/** A command the document calls, answered with what `handler` returns. */
		define(name: string, arity: number, handler: (...values: string[]) => unknown): void;
		forget(name: string): void;
		/** A file under the name a document uses for it. */
		provide(name: string, content: Uint8Array | string): void;
		withdraw(name: string): void;
		/** Whether the document was set with nothing reported. */
		typeset(document: string): boolean;
		/** Whether a piece of LaTeX alone was set, on a page cropped to it. */
		snippet(text: string): boolean;
		/** The last PDF made, in bytes of its own. */
		pdf(): Uint8Array;
		/** What went wrong, one error per line. */
		error(): string;
		delete(): void;
	}
}

export = Latex;
