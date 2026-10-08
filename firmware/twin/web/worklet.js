// The audio thread: plays what the twin's worker made (stereo, interleaved) and hands it the
// audio coming in (the mic or a line in, when one is connected). It tells the worker how many
// frames it has played in all, so the worker can stay just ahead.
class TwinOutput extends AudioWorkletProcessor
{
    constructor()
    {
        super();
        this.queue = [];      // Float32Array chunks, interleaved L R
        this.offset = 0;      // frames used of queue[0]
        this.played = 0;      // frames taken from the queue, in all
        this.underruns = 0;   // blocks that found nothing to play, once audio has started
        this.started = false;
        this.inChunk = new Float32Array(768 * 2);
        this.inFill = 0;
        this.worker = null;
        this.port.onmessage = (e) => {
            this.worker = e.data.port;
            this.worker.onmessage = (m) => {
                this.queue.push(m.data);
                this.started = true;
            };
        };
    }

    process(inputs, outputs)
    {
        const out = outputs[0];
        const frames = out[0].length;
        for (let i = 0; i < frames; i++)
        {
            if (this.queue.length === 0)
            {
                out[0][i] = 0;
                if (out[1]) out[1][i] = 0;
                if (i === 0 && this.started) this.underruns++;
                continue;
            }
            const chunk = this.queue[0];
            out[0][i] = chunk[this.offset * 2];
            if (out[1]) out[1][i] = chunk[this.offset * 2 + 1];
            this.offset++;
            this.played++;
            if (this.offset * 2 >= chunk.length)
            {
                this.queue.shift();
                this.offset = 0;
            }
        }

        const input = inputs[0];
        if (input && input.length > 0)
        {
            const l = input[0], r = input[1] || input[0];
            for (let i = 0; i < l.length; i++)
            {
                this.inChunk[this.inFill * 2] = l[i];
                this.inChunk[this.inFill * 2 + 1] = r[i];
                if (++this.inFill * 2 >= this.inChunk.length)
                {
                    if (this.worker) this.worker.postMessage({ input: this.inChunk });
                    this.inChunk = new Float32Array(768 * 2);
                    this.inFill = 0;
                }
            }
        }

        if (this.worker) this.worker.postMessage({ played: this.played, underruns: this.underruns });
        return true;
    }
}

registerProcessor('twin-output', TwinOutput);
