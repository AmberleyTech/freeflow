import SwiftUI
import AppKit

// Stats tab: an in-app front door to the FreeFlow Stats sidecar.
// It reads only the sidecar's aggregate stats.json (never transcripts) and
// opens the full local stats page. The sidecar remains a separate, optional
// component; when it is absent this tab shows install guidance instead.
struct StatsSettingsView: View {
    @State private var snapshot: SidecarStats?
    @State private var checked = false

    private var supportDir: URL {
        FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/FreeFlowStats", isDirectory: true)
    }

    var body: some View {
        Group {
            if let snapshot {
                content(snapshot)
            } else {
                missing
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .padding(24)
        .onAppear(perform: reload)
    }

    private func content(_ stats: SidecarStats) -> some View {
        VStack(alignment: .leading, spacing: 20) {
            Text("Dictation Stats")
                .font(.title2.weight(.bold))

            HStack(spacing: 12) {
                statCard("\(stats.streakDays)", "day streak")
                statCard("\(Int(stats.allTimeWPM.rounded()))", "dictation WPM")
                statCard("\(stats.transcriptions)", "transcriptions")
            }
            HStack(spacing: 12) {
                statCard("\(stats.words)", "words dictated")
                statCard(Self.humanize(stats.timeSavedSeconds), "time saved")
                statCard("\(Int(stats.typingWPM.rounded())) WPM", "typing speed")
            }

            HStack(spacing: 12) {
                Button("Open Full Stats Page") { openPage() }
                    .controlSize(.large)
                Button("Refresh") { reload() }
            }

            Text("Collected locally by the FreeFlow Stats sidecar. "
                 + "Aggregates only — transcripts are never stored. "
                 + "The full page auto-refreshes and includes the 14-day chart.")
                .font(.caption)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private var missing: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Dictation Stats")
                .font(.title2.weight(.bold))
            if checked {
                Text("The FreeFlow Stats sidecar is not installed yet.")
                    .font(.callout)
                Text("From your fork, run:\n\ncd StatsCompanion\nmake\nmake install\n\nThen dictate once and return here.")
                    .font(.callout)
                    .textSelection(.enabled)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Button("Check Again") { reload() }
        }
    }

    private func statCard(_ value: String, _ label: String) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(value)
                .font(.system(size: 24, weight: .bold, design: .rounded))
                .monospacedDigit()
            Text(label)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(
            RoundedRectangle(cornerRadius: 10)
                .fill(Color.primary.opacity(0.05))
        )
    }

    private func reload() {
        refreshSidecar()
        snapshot = SidecarStats.load(from: supportDir.appendingPathComponent("stats.json"))
        checked = true
    }

    private func openPage() {
        refreshSidecar()
        NSWorkspace.shared.open(supportDir.appendingPathComponent("stats.html"))
    }

    /// Best-effort: ask the sidecar to ingest the latest history before
    /// reading/opening. The binary finishes in milliseconds and exits.
    private func refreshSidecar() {
        let bin = supportDir.appendingPathComponent("bin/freeflow-stats")
        guard FileManager.default.isExecutableFile(atPath: bin.path) else { return }
        let task = Process()
        task.executableURL = bin
        try? task.run()
        task.waitUntilExit()
    }

    private static func humanize(_ seconds: Double) -> String {
        let s = Int(seconds.rounded())
        if s < 60 { return "\(s)s" }
        if s < 3600 { return "\(s / 60)m" }
        if s < 86400 { return "\(s / 3600)h \(s % 3600 / 60)m" }
        return "\(s / 86400)d \(s % 86400 / 3600)h"
    }
}

/// Parsed view of the sidecar's compact aggregate store:
/// {"tw": typingWPM, "tot": [count, words, seconds, ...], "d": [[date, count, words, seconds], ...]}
private struct SidecarStats {
    var transcriptions: Int
    var words: Int
    var typingWPM: Double
    var streakDays: Int
    var allTimeWPM: Double
    var timeSavedSeconds: Double

    static func load(from url: URL) -> SidecarStats? {
        guard let data = try? Data(contentsOf: url),
              let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let totals = root["tot"] as? [Any], totals.count >= 3,
              let count = (totals[0] as? NSNumber)?.intValue,
              let words = (totals[1] as? NSNumber)?.intValue,
              let seconds = (totals[2] as? NSNumber)?.doubleValue
        else { return nil }

        let typingWPM = (root["tw"] as? NSNumber)?.doubleValue ?? 40

        var activeDays: Set<String> = []
        if let days = root["d"] as? [Any] {
            for entry in days {
                if let row = entry as? [Any], let date = row.first as? String {
                    activeDays.insert(date)
                }
            }
        }

        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd"
        formatter.timeZone = .current
        let calendar = Calendar.current
        var day = calendar.startOfDay(for: Date())
        // A streak that ended yesterday still counts while today is young.
        if !activeDays.contains(formatter.string(from: day)),
           let yesterday = calendar.date(byAdding: .day, value: -1, to: day) {
            day = yesterday
        }
        var streak = 0
        while activeDays.contains(formatter.string(from: day)) {
            streak += 1
            guard let previous = calendar.date(byAdding: .day, value: -1, to: day) else { break }
            day = previous
        }

        let wpm = seconds > 0 ? Double(words) / (seconds / 60) : 0
        let typingSeconds = typingWPM > 0 ? Double(words) / typingWPM * 60 : 0
        return SidecarStats(
            transcriptions: count,
            words: words,
            typingWPM: typingWPM,
            streakDays: streak,
            allTimeWPM: wpm,
            timeSavedSeconds: max(0, typingSeconds - seconds)
        )
    }
}
