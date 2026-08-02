import { execFile } from "child_process";
import { promisify } from "util";

const execFileAsync = promisify(execFile);

export type ApprovalSource = "cursor" | "codex";

export type ApprovalPending = {
  id: ApprovalSource;
  source: ApprovalSource;
  title: string;
  body: string;
};

const APPROVAL_SCRIPT = `
on scanProcess(procName, sourceId)
  tell application "System Events"
    if not (exists process procName) then return ""
    tell process procName
      try
        repeat with w in windows
          set allText to ""
          try
            repeat with st in static texts of w
              try
                set allText to allText & (value of st as text) & " "
              end try
            end repeat
          end try
          try
            repeat with bt in buttons of w
              try
                set allText to allText & (name of bt as text) & " "
              end try
            end repeat
          end try
          if allText contains "Waiting for approval" or allText contains "waiting for approval" then
            return sourceId & "|" & procName & " needs approval"
          end if
          if allText contains "Approve" and allText contains "Run" then
            return sourceId & "|" & procName & " approve request"
          end if
        end repeat
      end try
    end tell
  end tell
  return ""
end scanProcess

set output to ""
set r to scanProcess("Cursor", "cursor")
if r is not "" then set output to output & r & linefeed
set r to scanProcess("Codex", "codex")
if r is not "" then set output to output & r
return output
`;

const TITLE_BY_SOURCE: Record<ApprovalSource, string> = {
  cursor: "Cursor",
  codex: "Codex",
};

function parseApprovalLine(line: string): ApprovalPending | null {
  const trimmed = line.trim();
  if (!trimmed) return null;
  const [source, ...rest] = trimmed.split("|");
  if (source !== "cursor" && source !== "codex") return null;
  const body = rest.join("|").trim() || "Waiting for approval";
  return {
    id: source,
    source,
    title: TITLE_BY_SOURCE[source],
    body,
  };
}

export async function scanApprovalRequests(): Promise<{
  ok: boolean;
  pending: ApprovalPending[];
  error?: string;
}> {
  try {
    const { stdout } = await execFileAsync("/usr/bin/osascript", ["-e", APPROVAL_SCRIPT], {
      timeout: 8000,
    });
    const pending = stdout
      .split("\n")
      .map(parseApprovalLine)
      .filter((item): item is ApprovalPending => item !== null);
    return { ok: true, pending };
  } catch (err) {
    return {
      ok: false,
      pending: [],
      error: err instanceof Error ? err.message : String(err),
    };
  }
}

export function approvalKey(pending: ApprovalPending[]): string {
  return pending
    .map((p) => `${p.source}:${p.body}`)
    .sort()
    .join(";");
}
