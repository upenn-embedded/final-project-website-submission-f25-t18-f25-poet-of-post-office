import sys
import numpy as np
import librosa
import soundfile as sf
import tensorflow as tf

# ====== 你的模型输入规格（要和训练一致） ======
SAMPLE_RATE = 16000
WIN_LENGTH   = 640     # 40ms
HOP_LENGTH   = 320     # 20ms
N_MELS       = 40
NUM_FRAMES   = 49      # 1秒 → (16000 - 640)/320 + 1 = 49
FEATURE_SIZE = NUM_FRAMES * N_MELS

CLASS_NAMES = ["Jarvis", "Other"]


def extract_logmel(samples, sample_rate):
    """复制 ESP audio_frontend.c 的特征逻辑"""
    # 1) 重采样
    if sample_rate != SAMPLE_RATE:
        samples = librosa.resample(samples, orig_sr=sample_rate, target_sr=SAMPLE_RATE)

    # 2) 如果不满 1 秒，补零；如果超过 1 秒，裁掉
    if len(samples) < SAMPLE_RATE:
        pad = SAMPLE_RATE - len(samples)
        samples = np.pad(samples, (0, pad))
    else:
        samples = samples[:SAMPLE_RATE]

    # 3) 计算 log-mel
    mel = librosa.feature.melspectrogram(
        y=samples,
        sr=SAMPLE_RATE,
        n_fft=WIN_LENGTH,
        hop_length=HOP_LENGTH,
        win_length=WIN_LENGTH,
        n_mels=N_MELS,
        fmin=20,
        fmax=4000
    )

    logmel = librosa.power_to_db(mel + 1e-6)
    logmel = logmel.astype(np.float32)

    # 4) reshape 成 (49,40,1)
    if logmel.shape[1] != NUM_FRAMES:
        # 如果帧数对不上（通常不会发生）
        logmel = librosa.util.fix_length(logmel, size=NUM_FRAMES, axis=1)

    return logmel.T  # shape = (49,40)


def load_tflite_model(path):
    interpreter = tf.lite.Interpreter(model_path=path)
    interpreter.allocate_tensors()
    return interpreter


def run_inference(interpreter, features):
    input_details  = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]

    # TFLite 预期 int8，先量化
    scale = input_details['quantization'][0]
    zero  = input_details['quantization'][1]

    q = np.round(features / scale + zero).astype(np.int8)
    q = np.expand_dims(q, axis=0)   # (1,49,40,1)

    interpreter.set_tensor(input_details['index'], q)
    interpreter.invoke()

    output = interpreter.get_tensor(output_details['index'])[0]

    # 反量化
    out_scale, out_zero = output_details['quantization']
    probs = (output.astype(np.float32) - out_zero) * out_scale

    return probs


def main():
    if len(sys.argv) < 2:
        print("Usage: python eval_one_wav.py your_audio.wav")
        sys.exit(0)

    wav_path = sys.argv[1]
    model_path = "model.tflite"   # 改成你的模型名字

    # 载入音频
    audio, sr = sf.read(wav_path)
    audio = audio.astype(np.float32)

    # 单声道
    if audio.ndim > 1:
        audio = audio[:, 0]

    # 提特征
    logmel = extract_logmel(audio, sr)
    logmel = logmel.reshape(49, 40, 1)

    # 加载模型
    interpreter = load_tflite_model(model_path)

    # 推理
    probs = run_inference(interpreter, logmel)

    pred = np.argmax(probs)
    print("\n========== RESULT ==========")
    print(f"Audio: {wav_path}")
    print(f"Pred : {pred} ({CLASS_NAMES[pred]})")
    print(f"Scores: Jarvis={probs[0]:.3f}, Other={probs[1]:.3f}")
    print("============================\n")


if __name__ == "__main__":
    main()
