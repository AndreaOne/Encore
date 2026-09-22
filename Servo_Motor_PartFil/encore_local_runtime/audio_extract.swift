import AVFoundation
import Foundation

enum AudioExtractError: Error {
    case invalidArguments
    case missingAudioTrack
    case readerOutput
    case readerStart
    case blockBuffer
    case sampleFormat
}

func littleEndianBytes<T: FixedWidthInteger>(_ value: T) -> [UInt8] {
    let little = value.littleEndian
    return withUnsafeBytes(of: little) { Array($0) }
}

do {
    guard CommandLine.arguments.count == 3 else {
        throw AudioExtractError.invalidArguments
    }

    let inputURL = URL(fileURLWithPath: CommandLine.arguments[1])
    let outputURL = URL(fileURLWithPath: CommandLine.arguments[2])
    let asset = AVURLAsset(url: inputURL)

    guard let track = asset.tracks(withMediaType: .audio).first else {
        throw AudioExtractError.missingAudioTrack
    }

    let outputSettings: [String: Any] = [
        AVFormatIDKey: kAudioFormatLinearPCM,
        AVLinearPCMIsBigEndianKey: false,
        AVLinearPCMIsFloatKey: false,
        AVLinearPCMBitDepthKey: 16,
        AVLinearPCMIsNonInterleaved: false,
    ]

    let reader = try AVAssetReader(asset: asset)
    let output = AVAssetReaderTrackOutput(track: track, outputSettings: outputSettings)
    output.alwaysCopiesSampleData = false
    guard reader.canAdd(output) else {
        throw AudioExtractError.readerOutput
    }
    reader.add(output)
    guard reader.startReading() else {
        throw AudioExtractError.readerStart
    }

    var sampleRate: UInt32 = 0
    var channelCount: UInt16 = 0
    if let anyFormatDescription = track.formatDescriptions.first {
        let formatDescription = anyFormatDescription as! CMAudioFormatDescription
        if let basic = CMAudioFormatDescriptionGetStreamBasicDescription(formatDescription) {
        sampleRate = UInt32(basic.pointee.mSampleRate.rounded())
        channelCount = UInt16(basic.pointee.mChannelsPerFrame)
        }
    }

    var pcmData = Data()
    while let sampleBuffer = output.copyNextSampleBuffer() {
        if sampleRate == 0 || channelCount == 0,
           let formatDescription = CMSampleBufferGetFormatDescription(sampleBuffer),
           let basic = CMAudioFormatDescriptionGetStreamBasicDescription(formatDescription) {
            sampleRate = UInt32(basic.pointee.mSampleRate.rounded())
            channelCount = UInt16(basic.pointee.mChannelsPerFrame)
        }

        guard let blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer) else {
            throw AudioExtractError.blockBuffer
        }

        let length = CMBlockBufferGetDataLength(blockBuffer)
        var chunk = Data(count: length)
        try chunk.withUnsafeMutableBytes { bytes in
            guard let baseAddress = bytes.baseAddress else {
                throw AudioExtractError.blockBuffer
            }
            let status = CMBlockBufferCopyDataBytes(
                blockBuffer,
                atOffset: 0,
                dataLength: length,
                destination: baseAddress
            )
            if status != noErr {
                throw AudioExtractError.blockBuffer
            }
        }
        pcmData.append(chunk)
    }

    if sampleRate == 0 || channelCount == 0 {
        throw AudioExtractError.sampleFormat
    }

    let bitsPerSample: UInt16 = 16
    let bytesPerSample = Int(bitsPerSample / 8)
    let byteRate = sampleRate * UInt32(channelCount) * UInt32(bytesPerSample)
    let blockAlign = channelCount * UInt16(bytesPerSample)
    let dataSize = UInt32(pcmData.count)
    let riffChunkSize = 36 + dataSize

    var wav = Data()
    wav.append(contentsOf: Array("RIFF".utf8))
    wav.append(contentsOf: littleEndianBytes(riffChunkSize))
    wav.append(contentsOf: Array("WAVE".utf8))
    wav.append(contentsOf: Array("fmt ".utf8))
    wav.append(contentsOf: littleEndianBytes(UInt32(16)))
    wav.append(contentsOf: littleEndianBytes(UInt16(1)))
    wav.append(contentsOf: littleEndianBytes(channelCount))
    wav.append(contentsOf: littleEndianBytes(sampleRate))
    wav.append(contentsOf: littleEndianBytes(byteRate))
    wav.append(contentsOf: littleEndianBytes(blockAlign))
    wav.append(contentsOf: littleEndianBytes(bitsPerSample))
    wav.append(contentsOf: Array("data".utf8))
    wav.append(contentsOf: littleEndianBytes(dataSize))
    wav.append(pcmData)

    try wav.write(to: outputURL)
} catch {
    fputs("\(error)\n", stderr)
    exit(1)
}
