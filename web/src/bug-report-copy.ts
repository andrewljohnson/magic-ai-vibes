export type BugReportCopyState = "idle" | "copying" | "copied" | "failed";

type ClipboardTextArea = {
  value: string;
  style: {
    position: string;
    opacity: string;
  };
  setAttribute(name: string, value: string): void;
  select(): void;
  remove(): void;
};

type ClipboardDependencies = {
  writeText?: (text: string) => Promise<void>;
  createTextArea(): ClipboardTextArea;
  appendTextArea(field: ClipboardTextArea): void;
  execCopy(): boolean;
};

function browserClipboardDependencies(): ClipboardDependencies {
  return {
    ...(navigator.clipboard?.writeText
      ? {
          writeText: (text: string) => navigator.clipboard.writeText(text),
        }
      : {}),
    createTextArea: () => document.createElement("textarea"),
    appendTextArea: (field) =>
      document.body.append(field as HTMLTextAreaElement),
    execCopy: () => document.execCommand("copy"),
  };
}

export async function copyTextToClipboard(
  text: string,
  dependencies: ClipboardDependencies = browserClipboardDependencies(),
): Promise<void> {
  if (dependencies.writeText) {
    try {
      await dependencies.writeText(text);
      return;
    } catch {
      // A denied or unavailable async clipboard still permits the legacy path
      // in some browsers and non-secure local development contexts.
    }
  }

  const field = dependencies.createTextArea();
  try {
    field.value = text;
    field.setAttribute("readonly", "");
    field.style.position = "fixed";
    field.style.opacity = "0";
    dependencies.appendTextArea(field);
    field.select();
    if (!dependencies.execCopy()) {
      throw new Error("Clipboard copy is unavailable");
    }
  } finally {
    field.remove();
  }
}

export type BugReportCopyResult = "copied" | "failed" | "stale";

type BugReportCopyOperation = {
  gameId: string;
  generation: number;
  signal: AbortSignal;
  currentGeneration(): number;
  fetchReport(id: string, signal: AbortSignal): Promise<unknown>;
  copyText(text: string): Promise<void>;
  updateState(state: "copied" | "failed"): void;
};

function isCurrent(operation: BugReportCopyOperation): boolean {
  return (
    !operation.signal.aborted &&
    operation.currentGeneration() === operation.generation
  );
}

export async function performBugReportCopy(
  operation: BugReportCopyOperation,
): Promise<BugReportCopyResult> {
  try {
    const report = await operation.fetchReport(
      operation.gameId,
      operation.signal,
    );
    if (!isCurrent(operation)) return "stale";

    await operation.copyText(JSON.stringify(report, null, 2));
    if (!isCurrent(operation)) return "stale";

    operation.updateState("copied");
    return "copied";
  } catch {
    if (!isCurrent(operation)) return "stale";

    operation.updateState("failed");
    return "failed";
  }
}
