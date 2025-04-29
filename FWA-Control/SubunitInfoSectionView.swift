import SwiftUI

struct SubunitInfoSectionView: View {
    var musicSubunit: MusicSubunitInfo?
    var audioSubunit: AudioSubunitInfo?
    // Add other potential subunits here

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("SUBUNITS")
                .font(.caption.weight(.semibold))
                .foregroundColor(.secondary)
                .padding(.bottom, 4)

            if musicSubunit != nil || audioSubunit != nil {
                if let subunit = musicSubunit {
                    SubunitPresenceRow(name: subunit.typeName, typeCode: SubunitType.music.rawValue, isPresent: true)
                } else {
                    SubunitPresenceRow(name: "Music", typeCode: SubunitType.music.rawValue, isPresent: false)
                }

                if let subunit = audioSubunit {
                     SubunitPresenceRow(name: subunit.typeName, typeCode: SubunitType.audio.rawValue, isPresent: true)
                } else {
                     SubunitPresenceRow(name: "Audio", typeCode: SubunitType.audio.rawValue, isPresent: false)
                }
                // Add rows for other potential subunits
            } else {
                 Text("No subunits detected.")
                     .foregroundColor(.secondary)
                     .italic()
            }
        }
    }
}

// Helper View for Subunit Presence Row
struct SubunitPresenceRow: View {
    var name: String
    var typeCode: UInt8
    var isPresent: Bool

    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: name == "Music" ? "music.note" : name == "Audio" ? "speaker.wave.2" : "questionmark.square.dashed")
                .foregroundColor(isPresent ? .accentColor : .secondary)
                .imageScale(.medium)
            Text("\(name) Subunit (0x\(String(format: "%02X", typeCode)))")
                .fontWeight(.medium)
            Spacer()
            HStack(spacing: 4) {
                Image(systemName: isPresent ? "checkmark.circle.fill" : "xmark.circle")
                    .foregroundColor(isPresent ? .green : .secondary)
                Text(isPresent ? "Present" : "Absent")
                    .foregroundColor(isPresent ? .green : .secondary)
                    .font(.caption)
            }
        }
        .padding(.vertical, 6)
        .padding(.horizontal, 10)
    }
}
