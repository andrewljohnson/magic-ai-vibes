export class ApiRequestError extends Error {
  readonly status: number;
  readonly code: string;
  readonly details?: unknown;

  constructor(
    status: number,
    code: string,
    message: string,
    details?: unknown,
  ) {
    super(message);
    this.name = "ApiRequestError";
    this.status = status;
    this.code = code;
    this.details = details;
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

export function apiRequestErrorFromResponse(
  status: number,
  body: unknown,
): ApiRequestError {
  const rawError = isRecord(body) ? body.error : undefined;
  const error = isRecord(rawError) ? rawError : null;
  const message =
    typeof error?.message === "string"
      ? error.message
      : typeof rawError === "string"
        ? rawError
        : `Request failed (${status})`;
  return new ApiRequestError(
    status,
    typeof error?.code === "string" ? error.code : "http_error",
    message,
    error?.details,
  );
}
