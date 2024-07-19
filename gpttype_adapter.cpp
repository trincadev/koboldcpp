//This is Concedo's shitty adapter for adding python bindings for llama

//Considerations:
//Don't want to use pybind11 due to dependencies on MSVCC
//ZERO or MINIMAL changes as possible to main.cpp - do not move their function declarations here!
//Leave main.cpp UNTOUCHED, We want to be able to update the repo and pull any changes automatically.
//No dynamic memory allocation! Setup structs with FIXED (known) shapes and sizes for ALL output fields
//Python will ALWAYS provide the memory, we just write to it.

#include <cmath>
#include <time.h>
#include <mutex>
#include <unordered_map>
#include "model_adapter.h"
#include "otherarch.h"
#include "grammar-parser.h"

//for easier compilation
//concat source files into one file for compilation purposes
#include "llama_v2.cpp"
#include "llama_v3.cpp"
#include "src/llama.cpp"
#include "utils.cpp"
#include "gptj_v1.cpp"
#include "gptj_v2.cpp"
#include "gptj_v3.cpp"
#include "gpt2_v1.cpp"
#include "gpt2_v2.cpp"
#include "gpt2_v3.cpp"
#include "rwkv_v2.cpp"
#include "rwkv_v3.cpp"
#include "neox_v2.cpp"
#include "neox_v3.cpp"
#include "mpt_v3.cpp"
#include "examples/llava/clip.h"
#include "examples/llava/llava.h"

//const
const int extra_context_handle_fragmentation = 80;
const int LLAVA_TOKEN_IDENTIFIER_A = -998; //alternate between both, changing when image changes
const int LLAVA_TOKEN_IDENTIFIER_B = -999;

//shared
std::string executable_path = "";
std::string lora_filename = "";
std::string lora_base = "";
std::string mmproj_filename = "";
bool generation_finished;
float last_process_time = 0;
float last_eval_time = 0;
int last_token_count = 0;
int last_seed = -1;
int total_gens = 0;
stop_reason last_stop_reason = stop_reason::INVALID;
std::vector<std::string> generated_tokens;

llama_grammar *  grammar = nullptr; //currently used grammar
grammar_parser::parse_state parsed_grammar;
static std::string current_grammar = "";

//return val: 0=fail, 1=(original ggml, alpaca), 2=(ggmf), 3=(ggjt)
static FileFormat file_format = FileFormat::BADFORMAT;
static FileFormatExtraMeta file_format_meta;

static gpt_vocab vocab;
static int32_t n_vocab = 0;

static gptj_v1_model gptj_ctx_v1;
static gptj_v2_model gptj_ctx_v2;
static gptj_model gptj_ctx_v3;

static gpt2_v1_model gpt2_ctx_v1;
static gpt2_v2_model gpt2_ctx_v2;
static gpt2_model gpt2_ctx_v3;

static gpt_neox_v2_model neox_ctx_v2;
static gpt_neox_model neox_ctx_v3;

static mpt_model mpt_ctx_v3;

static rwkv_v2_context * rwkv_ctx_v2;
static rwkv_context * rwkv_ctx_v3;

static llama_v2_context * llama_ctx_v2;
static llama_v3_context * llama_ctx_v3;
static llama_context * llama_ctx_v4;

static clip_ctx * clp_ctx = nullptr; //for llava
static clip_image_u8 * clp_img_data = nullptr; //most recent image
static std::vector<llava_image> llava_images;
static std::string llava_composite_image_signature = ""; //for identifying when the llava images change, we need to invalidate the cache
static int current_llava_identifier = LLAVA_TOKEN_IDENTIFIER_A;

static gpt_params * kcpp_params = nullptr;
static int max_context_limit_at_load = 0;
static int n_past = 0;
static bool useSmartContext = false;
static bool useContextShift = false;
static int debugmode = 0; //-1 = hide all, 0 = normal, 1 = showall
static std::string modelname;
static std::vector<gpt_vocab::id> last_n_tokens;
static std::vector<gpt_vocab::id> current_context_tokens;
static size_t mem_per_token = 0;
static std::vector<float> logits;
static std::vector<int> smartcontext;
static std::vector<std::string> stop_sequence;
static std::vector<int> special_stop_sequence; //for stop sequences that don't have a string representation
static std::vector<std::string> banned_tokens;
static std::vector<int> banned_token_ids;
static std::unordered_multimap<gpt_vocab::id, std::vector<gpt_vocab::id>> dry_sequence_breakers; // Multi-mapping from first token of sequence to tail of sequence (tail is empty for a single token)
static std::vector<int> dry_repeat_count; // Indexed as last_n_tokens
static std::unordered_map<gpt_vocab::id, int> dry_max_token_repeat;
static std::vector<llama_token_data> top_picks;
static int remaining_tokens = 0;
static int stopper_unused_tokens = 0;
static std::mutex concat_output_mtx;
static std::string concat_output = "";
static std::string concat_output_reader_copy_poll = ""; //for streaming
static std::string concat_output_reader_copy_res = ""; //for gen response
static std::vector<logit_bias> logit_biases;

inline bool IsNanCheck(float f)
{
    const unsigned int u = *(unsigned int*)&f;
    return (u&0x7F800000) == 0x7F800000 && (u&0x7FFFFF);    // Both NaN and qNan.
}

inline bool LogitsDuplicated(std::vector<float> & arr1, std::vector<float> & arr2)
{
    int compareQty = 5;
    if(arr1.size() < compareQty || arr2.size() < compareQty || arr1.size()!=arr2.size())
    {
        printf("\nError: Logit array sizes are bad!\n");
        return false;
    }
    for(int i=0;i<compareQty;++i)
    {
        if(arr1[i]!=arr2[i])
        {
            return false;
        }
    }
    return true;
}


static std::string FileFormatTokenizeID(int id, FileFormat file_format, bool return_special = false)
{
    if (file_format == FileFormat::GGML || file_format == FileFormat::GGHF || file_format == FileFormat::GGJT || file_format == FileFormat::GGJT_2)
    {
        return std::string(llama_v2_token_to_str(llama_ctx_v2, id));
    }
    else if (file_format == FileFormat::GGJT_3)
    {
        return std::string(llama_v3_token_to_str(llama_ctx_v3, id));
    }
    else if(file_format == FileFormat::GGUF_GENERIC)
    {
        return std::string(llama_token_to_piece(llama_ctx_v4, id, return_special));
    }
    else
    {
        return vocab.id_to_token[id];
    }
}

static void TokenizeString(const std::string & str_to_tokenize, std::vector<int> & output_tokens, FileFormat file_format, bool add_bos=true)
{
    if (file_format == FileFormat::GGML || file_format == FileFormat::GGHF || file_format == FileFormat::GGJT || file_format == FileFormat::GGJT_2  || file_format == FileFormat::GGJT_3 || file_format == FileFormat::GGUF_GENERIC)
    {
        if(file_format == FileFormat::GGHF || file_format == FileFormat::GGJT || file_format == FileFormat::GGJT_2 )
        {
            output_tokens = ::llama_v2_tokenize(llama_ctx_v2, str_to_tokenize, add_bos);
        }
        else if (file_format == FileFormat::GGML)
        {
            output_tokens = ::legacy_llama_v2_tokenize(llama_ctx_v2, str_to_tokenize, add_bos);
        }
        else if (file_format == FileFormat::GGJT_3)
        {
            output_tokens = ::llama_v3_tokenize(llama_ctx_v3, str_to_tokenize, add_bos);
        }
        else
        {
            output_tokens = ::llama_tokenize(llama_ctx_v4, str_to_tokenize, add_bos, true);
            if(add_bos)
            {
                llama_token bostoadd = llama_token_bos(&(llama_ctx_v4->model));
                if(output_tokens.size()==0)
                {
                    output_tokens.push_back(bostoadd);
                }
                else
                {
                    if(output_tokens[0]!=bostoadd)
                    {
                        output_tokens.insert(output_tokens.begin(), 1, bostoadd);
                    }
                }
            }
        }
    }
    else
    {
        // tokenize the prompt
        output_tokens = ::gpt_tokenize(vocab, str_to_tokenize);
    }
}
static int GetEosID(FileFormat file_format, int32_t n_vocab)
{
    unsigned int eosID = 0;

    if(file_format == FileFormat::GGML || file_format == FileFormat::GGHF || file_format == FileFormat::GGJT || file_format == FileFormat::GGJT_2 || file_format == FileFormat::GGJT_3 || file_format == FileFormat::GGUF_GENERIC)
    {
        if(file_format == FileFormat::GGUF_GENERIC)
        {
            eosID = llama_token_eos(&(llama_ctx_v4->model));
        }
        else if(file_format == FileFormat::GGJT_3)
        {
            eosID = llama_v3_token_eos();
        }
        else
        {
            eosID = llama_v3_token_eos();
        }
    }
    else
    {
        if (file_format == FileFormat::GPT2_1 ||
        file_format == FileFormat::GPT2_2 ||
        file_format == FileFormat::GPT2_3 ||
        file_format == FileFormat::GPT2_4 ||
        file_format == FileFormat::GPTJ_1 ||
        file_format == FileFormat::GPTJ_2 ||
        file_format == FileFormat::GPTJ_3 ||
        file_format == FileFormat::GPTJ_4 ||
        file_format == FileFormat::GPTJ_5)
        {
            eosID = 50256;
            if (n_vocab <= eosID)
            {
                //special case, starcoder models use ID 0 for EOS
                eosID = 0;
            }
        }

        if (file_format == FileFormat::RWKV_1 ||
            file_format == FileFormat::RWKV_2 ||
            file_format == FileFormat::NEOX_1 ||
            file_format == FileFormat::NEOX_2 ||
            file_format == FileFormat::NEOX_3 ||
            file_format == FileFormat::NEOX_4 ||
            file_format == FileFormat::NEOX_5 ||
            file_format == FileFormat::NEOX_6 ||
            file_format == FileFormat::NEOX_7 ||
            file_format == FileFormat::MPT_1)
        {
            eosID = 0;
        }
    }
    return eosID;
}
static int GetEotID(FileFormat file_format)
{
    if(file_format == FileFormat::GGUF_GENERIC)
    {
        return llama_token_eot(&(llama_ctx_v4->model));
    }
    return -1;
}

static float LowestLogit(const std::vector<float> & logits)
{
    int topid = std::min_element(logits.begin(), logits.end()) - logits.begin();
    float v = logits[topid];
    return (v < 0 ? (v-8) : 0);
}
static float LowestLogit(const float *logits, size_t size)
{
    if (size == 0) {
        // Handle the case of an empty array
        return 0.0;
    }
    int topid = std::min_element(logits, logits + size) - logits;
    float v = logits[topid];
    return (v < 0 ? (v-8) : 0);
}

static std::string RemoveBell(const std::string & input) //removes the bell character
{
    std::string word2;
    std::remove_copy(input.begin(), input.end(), std::back_inserter(word2), '\a');
    return word2;
}

static std::string get_tok_vec_str(std::vector<int> &embd)
{
    std::string tmp = "";
    for (auto id : embd)
    {
        tmp += "'" + FileFormatTokenizeID(id, file_format, true) + " (" + std::to_string(id) + ")', ";
    }
    ::utreplace(tmp, "\n", "\\n");
    return tmp;
}
static void print_tok_vec_str(std::vector<int> &vec)
{
    printf("\n%s", get_tok_vec_str(vec).c_str());
}

// Find tokens that completely contain `str`, either as a single token, or as a sequence of tokens.
// It's important to use a hash map for head tokens because some models have many of them.
// For example, the Llama 3 tokenizer has 6570 tokens containing the period ('.') character.
// Single tokens are allowed to extend past `str` at the front and back. This is to allow, for
// instance, the token '.\n' to be a head for both '.' and '\n'. However if a head token
// begins a multi-token sequence, the head can only extend past `str` at the beginning. The
// tail tokens are generated by tokenizing the remainder.
// If max_tail_len is >= 0, the maximum token length of a tail sequence is clamped to this value.
static void GetOverlappingTokenSequences(const std::string& str, std::unordered_multimap<gpt_vocab::id, std::vector<gpt_vocab::id>>& token_sequences, int max_tail_len = -1) {
    for(int v=0;v<n_vocab;++v)
    {
        std::string word = FileFormatTokenizeID(v, file_format, true);
        if (word.find(str) != std::string::npos)
        {
            // The string is entirely contained within this single token.
            // Ensure that token_sequences only contains one key-value-pair with an empty value.
            auto its = token_sequences.equal_range(v);
            bool empty = false;
            for (auto it = its.first; it != its.second; ++it) {
                if (it->second.empty()) {
                    empty = true;
                    break;
                }
            }
            if (!empty) {
                token_sequences.emplace(v, std::vector<gpt_vocab::id>());
            }
        } else {
            // Check whether a prefix of the string overlaps with a suffix of the token.
            // Just do a naive O(N^2) search, since the worst case is limited by the
            // maximum character length of a token in the vocabulary.
            size_t word_len = word.size(), str_len = str.size();
            size_t pos = -1;
            while ((pos = word.find(str[0], pos + 1)) != std::string::npos) {
                bool match = true;
                size_t i;
                for (i = 1; i < str_len && i + pos < word_len; ++i) {
                    if (word[pos + i] != str[i]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    // We matched to the end of the string. Since `str` is not contained in `word`,
                    // there must be trailing letters in `str`.
                    std::vector<gpt_vocab::id> tokenization;
                    TokenizeString(str.substr(i), tokenization, file_format, false);
                    if (max_tail_len >= 0 && tokenization.size() > max_tail_len) {
                        tokenization.resize(max_tail_len);
                    }

                    // Ensure we don't already have a duplicate matching tokenization.
                    auto its = token_sequences.equal_range(v);
                    bool found = false;
                    for (auto it = its.first; it != its.second; ++it) {
                        if (tokenization == it->second) {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        token_sequences.emplace(v, tokenization);
                    }
                }
            }
        }
    }
}

llama_token sample_token(llama_token_data_array * candidates, std::mt19937 & rng)
{
    llama_sample_softmax(nullptr, candidates);
    std::vector<float> probs;
    probs.reserve(candidates->size);
    top_picks.clear();
    for (size_t i = 0; i < candidates->size; ++i) {
        probs.push_back(candidates->data[i].p);
    }

    std::discrete_distribution<> dist(probs.begin(), probs.end());
    int idx = dist(rng);

    if(debugmode==1)
    {
        top_picks.push_back(candidates->data[idx]);
        for (size_t i = 0; (i < candidates->size && i<4); ++i)
        {
            if(i!=idx)
            {
                top_picks.push_back(candidates->data[i]);
            }
        }
    }

    llama_token result = candidates->data[idx].id;
    return result;
}

llama_token sample_token_mirostat(int n_vocab, llama_token_data_array * candidates, std::mt19937 & rng, float tau, float eta, int m, float * mu)
{
    float N = float(n_vocab);
    llama_sample_softmax(nullptr, candidates);
    // Estimate s_hat using the most probable m tokens
    float s_hat = 0.0;
    float sum_ti_bi = 0.0;
    float sum_ti_sq = 0.0;
    for (size_t i = 0; i < size_t(m - 1) && i < candidates->size - 1; ++i) {
        float t_i = logf(float(i + 2) / float(i + 1));
        float b_i = logf(candidates->data[i].p / candidates->data[i + 1].p);
        sum_ti_bi += t_i * b_i;
        sum_ti_sq += t_i * t_i;
    }
    s_hat = sum_ti_bi / sum_ti_sq;
    // Compute k from the estimated s_hat and target surprise value
    float epsilon_hat = s_hat - 1;
    float k = powf((epsilon_hat * powf(2, *mu)) / (1 - powf(N, -epsilon_hat)), 1 / s_hat);
    // Sample the next word X using top-k sampling
    llama_sample_top_k(nullptr, candidates, int(k),1);
    llama_token X = sample_token(candidates, rng);    // Compute error as the difference between observed surprise and target surprise value
    size_t X_idx = std::distance(candidates->data, std::find_if(candidates->data, candidates->data + candidates->size, [&](const llama_token_data & candidate) {
        return candidate.id == X;
    }));
    float observed_surprise = -log2f(candidates->data[X_idx].p);
    float e = observed_surprise - tau;
    // Update mu using the learning rate and error
    *mu = *mu - eta * e;
    return X;
}

llama_token sample_token_mirostat_v2(llama_token_data_array * candidates, std::mt19937 & rng, float tau, float eta, float * mu)
{
    llama_sample_softmax(nullptr, candidates);
    // Truncate the words with surprise values greater than mu
    candidates->size = std::distance(candidates->data, std::find_if(candidates->data, candidates->data + candidates->size, [&](const llama_token_data & candidate) {
        return -log2f(candidate.p) > *mu;
    }));

    if (candidates->size == 0) {
        candidates->size = 1;
    }

    // Normalize the probabilities of the remaining words
    llama_sample_softmax(nullptr, candidates);
    // Sample the next word X from the remaining words
    llama_token X = sample_token(candidates,rng);

    // Compute error as the difference between observed surprise and target surprise value
    size_t X_idx = std::distance(candidates->data, std::find_if(candidates->data, candidates->data + candidates->size, [&](const llama_token_data & candidate) {
        return candidate.id == X;
    }));
    float observed_surprise = -log2f(candidates->data[X_idx].p);
    float e = observed_surprise - tau;
    // Update mu using the learning rate and error
    *mu = *mu - eta * e;
    return X;
}

// Top-a (remove all tokens that have softmax probability less than top_a*m^2 where m is the maximum softmax probability)
// top-a 0 is off (no effect)
void sample_top_a(llama_token_data_array * candidates, float a, size_t min_keep) {
    if (a <= 0.0f || candidates->size<=1) {
        return;
    }

    llama_sample_softmax(nullptr, candidates);

    // Compute the cumulative probabilities
    float maxprob = candidates->data[0].p;

    float threshold = a * maxprob * maxprob; //tokens with probs less than this are removed
    size_t last_idx = candidates->size;

    for (size_t i = 0; i < candidates->size; ++i) {
        // Go until we reach a value under the threshold
        float checkprob = candidates->data[i].p;
        if (checkprob < threshold && i >= min_keep) {
            last_idx = i;
            break;
        }
    }
    // printf("\n\nCandidates: %d, A:%f, MaxProb: %f, Threshold: %f, LastIdx: %d",candidates->size,a,maxprob,threshold,last_idx);
    // printf("\nCandidates: %f %f %f %f\n",candidates->data[0].p,candidates->data[1].p,candidates->data[2].p,candidates->data[3].p);

    // Resize the output vector to keep only the selected tokens
    candidates->size = last_idx;
}

void sample_dry(int n_ctx, int penalty_range, float penalty_multiplier, float penalty_base, int allowed_length, const std::unordered_multimap<gpt_vocab::id, std::vector<gpt_vocab::id>>& restart_sequences, llama_token_data_array * candidates) {
    if (penalty_multiplier == 0.0f || penalty_base == 0.0f) {
        return;
    }
    if (penalty_range <= 0) {
        penalty_range = n_ctx;
    }
    auto last_n_repeat = std::min(std::min((int)current_context_tokens.size(), penalty_range), n_ctx);
    if (last_n_repeat <= allowed_length) {
        return;
    }
    const llama_token * last_tokens = current_context_tokens.data() + current_context_tokens.size() - last_n_repeat;

    dry_repeat_count.assign(last_n_repeat, 0);
    dry_max_token_repeat.clear();

    // Step 1: Look for restart sequences to limit the maximum repetition length.
    // Work backwards through the context looking for any token that begins a restart sequence.
    //
    // The collection `restart_sequences` is a mapping from a "head" token to all "tail"
    // sequences that together comprise a restart sequence. This allows us to quickly check
    // whether each token is the head of a complete sequence. Most restart sequences are actually
    // a single token, and for these the "tail" is an empty vector.
    //
    // If the token is a "head", test all restart sequences that begin with this token
    // (there will often only be one sequence for each token, but if sequences like 'aaaq1' and
    // 'aaa1' are used as restart strings, both could start with 'aaa' when tokenized). The
    // longest matching sequence (if any) is used to limit the maximum repetition length.
    //
    // Note that in the case case of a short sequence contained in a longer one, this might fail to
    // find the smallest value for `rep_limit`. For example, if 'amniotic' and 'ni' are both used as
    // restart sequences, 'ni' will be found first, and since it's shorter it will fail to suppress
    // 'otic'. This is a minor issue since fully contained restart sequences are likely to be rare.
    //
    // This is theoretically worst-case O(N^2) for arbitrary restart sequences, which is why we
    // have already clamped the maximum tail sequence length when generating `restart_sequences`.
    // With clamping, this scan is O(N) in the context length.

    int rep_limit = last_n_repeat;
    for (size_t i = 0; i < last_n_repeat; ++i) {
        size_t ix = last_n_repeat - 1 - i;
        auto its = restart_sequences.equal_range(last_tokens[ix]);
        if (its.first == restart_sequences.end()) {
            continue;
        }
        int longest_match = -1;
        for (auto it = its.first; it != its.second; ++it) {
            // Note that (*it) does not contain the head character, so seq_len will be
            // the restart sequence length minus 1.
            // In the common case of a single-token restart sequence, (*it) will be empty
            // and we will trivially match.
            int seq_len = (int)it->second.size();
            if (seq_len > longest_match && seq_len <= i) {
                bool match = true;
                for (size_t offset = 0; offset < seq_len; ++offset) {
                    // The +1 when indexing `last_tokens` is because we already matched the head.
                    if (it->second[offset] != last_tokens[ix + 1 + offset]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    longest_match = seq_len;
                }
            }
        }
        if (longest_match >= 0) {
            // We found a restart sequence starting `i` tokens from the end and continuing for
            // `longest_match` tokens.
            rep_limit = (int)i - longest_match;
            break;
        }
    }
    if (rep_limit <= allowed_length) {
        return;
    }

    // Step 2: Iterate in reverse over the last N tokens of the context, using the "Z-algorithm" (in
    // the reverse direction) to efficiently compute the positions and lengths of suffixes appearing
    // elsewhere in the context. We limit the suffix length to `rep_limit` to respect restart sequences.
    //
    // This algorithm is not currently documented on Wikipedia, but there is a clear description here:
    // https://ivanyu.me/blog/2014/10/15/z-algorithm/
    //
    // The code below is adapted from the public domain implementation by the same author here:
    // https://github.com/ivanyu/string-algorithms/blob/master/z_algorithm.py
    //
    // Example:
    // Last N tokens: a b c c b c y a b c
    // Repeat counts: 0 0 3 1 0 2 0 0 0 0
    //                    ^
    //   This `3` means that the last three tokens of the context (a b c) also appear here.
    //
    // This step is worst case O(N) since the Z-algorithm is linear, despite the appearance of nested
    // for/while loops. This can be seen by observing that the `lt` and `rt` bounds are set after each
    // repeated suffix is detected (i.e. after each while loop when n > 0). These bound variables
    // ensure that the inner while loops only examine each token in the context once as the outer
    // for loop iterates over the context.

    {
        const int last = last_n_repeat - 1;
        int rt = 0, lt = 0;

        for (int k = 1; k < last_n_repeat; ++k) {
            if (k > rt) {
                // If k is outside the current Z-box, do naive computation.
                int n = 0;
                while (n + k < last_n_repeat && last_tokens[last - n] == last_tokens[last - (n+k)]) {
                    ++n;
                }
                dry_repeat_count[last - k] = std::min(n, rep_limit);
                if (n > 0) {
                    lt = k;
                    rt = k+n-1;
                }
            } else {
                // If k is inside the current Z-box, consider two cases.

                int p = k - lt; // Pair index.
                int right_part_len = rt - k + 1;

                if (dry_repeat_count[last - p] < right_part_len) {
                    int n = std::min(dry_repeat_count[last - p], rep_limit);
                    dry_repeat_count[last - k] = n;
                } else {
                    int i = rt + 1;
                    while (i < last_n_repeat && last_tokens[last - i] == last_tokens[last - (i - k)]) {
                        i += 1;
                    }

                    int n = std::min(i - k, rep_limit);
                    dry_repeat_count[last - k] = n;

                    lt = k;
                    rt = i - 1;
                }
            }
        }
    }

    // Step 3: Iterate over dry_repeat_count and last_tokens, examining the maximum repeat length
    // that would be generated by emitting each new token that would extend a sequence.
    //
    // Following the same example as above:
    // Last N tokens: a b c c b c y a b c
    // Repeat counts: 0 0 3 1 0 2 0 0 0 0
    //
    // For each non-zero, look ahead one token. This token, if emitted, would extend the repetition.
    // c: 3 -> 4 (from `a b c` to `a b c c`)
    // b: 1 -> 2 (from `c` to `c b`)
    // y: 2 -> 3 (from `b c` to `b c y`)

    for (size_t i = 0; i < last_n_repeat - 1; ++i) {
        int repeat_len = dry_repeat_count[i];
        if (repeat_len >= allowed_length) {
            // This token ends a repeat, so the next token would continue one.
            // By convention, the value of `repeat_len` only includes the tokens currently
            // in the context, not the new token that would be added.
            gpt_vocab::id token = last_tokens[i + 1];
            // Track the maximum sequence ending in this token.
            const auto& it = dry_max_token_repeat.find(token);
            if (it == dry_max_token_repeat.end() || it->second < repeat_len) {
                dry_max_token_repeat[token] = repeat_len;
            }
        }
    }

    // Step 4: Apply logit penalties based on the maximum repeat length for relevant tokens.

    // Prevent floating point overflow in `pow(penalty_base, exponent)` by clamping to `max_exponent`.
    // Compute it from `penalty_base` and the approximate log of `std::numeric_limits<float>::max()`
    const float FLOAT_MAX_LOG = 88.7228391f;
    int max_exponent = 0;
    if (penalty_base > 1.000001f) {
        max_exponent = FLOAT_MAX_LOG / std::log(penalty_base);
    }

    if (debugmode==1 && !dry_max_token_repeat.empty()) {
        printf("DRY penalties [");
    }
    size_t count = 0;
    for (const auto& kvp: dry_max_token_repeat) {
        gpt_vocab::id token = kvp.first;
        int repeat_exp = kvp.second - allowed_length;
        if (max_exponent > 0 && repeat_exp > max_exponent) {
            repeat_exp = max_exponent;
        }
        float penalty = penalty_multiplier * pow(penalty_base, repeat_exp);
        if (debugmode==1)
        {
            std::string tokenizedstr = FileFormatTokenizeID(token, file_format);
            ::utreplace(tokenizedstr, "\n", "\\n");
            printf("%s(%s %.02f)", count == 0 ? "" : " ", RemoveBell(tokenizedstr).c_str(), penalty);
        }
        candidates->data[token].logit -= penalty;
        ++count;
    }
    if (debugmode==1 && !dry_max_token_repeat.empty()) {
        printf("]\n");
    }
}

void sample_rep_pen(int n_ctx, int rep_pen_range, float rep_pen, float rep_pen_slope, float presence_penalty, llama_token_data_array * candidates_p)
{
    auto last_n_repeat = std::min(std::min((int)last_n_tokens.size(), rep_pen_range), n_ctx);

    const llama_token * last_tokens =  last_n_tokens.data() + last_n_tokens.size() - last_n_repeat;
    size_t last_tokens_size = last_n_repeat;
    llama_token_data_array * candidates = candidates_p;

    if (last_tokens_size == 0 || (rep_pen == 1.0f && presence_penalty==0)) {
        return;
    }

    const int64_t t_start_sample_us = ggml_time_us();

    // Create a frequency map to count occurrences of each token in last_tokens
    std::unordered_map<llama_token, int> token_count_near;
    std::unordered_map<llama_token, int> token_count_far;
    for (size_t i = 0; i < last_n_repeat; ++i) {
        if((i*2) >= last_n_repeat)
        {
            token_count_near[last_tokens[i]]++;
        }
        else
        {
            token_count_far[last_tokens[i]]++;
        }
    }

    float rep_pen_reduced = rep_pen;
    if(rep_pen_reduced>1.0f)
    {
       rep_pen_reduced = 1.0f + ((rep_pen-1.0f)*rep_pen_slope);
    }
    for (size_t i = 0; i < candidates->size; ++i) {
        const auto token_in_near = token_count_near.find(candidates->data[i].id);
        const auto token_in_far = token_count_far.find(candidates->data[i].id);
        bool in_near = (token_in_near != token_count_near.end());
        bool in_far = (token_in_far != token_count_far.end());
        if (!in_near && !in_far) {
            continue;
        }

        float penalty = (in_near?rep_pen:rep_pen_reduced);

        // The academic publication that described this technique actually just only divided, but that would cause tokens with negative logits to become more likely, which is obviously wrong.
        // This is common fix for this problem, which is to multiply by the penalty instead of dividing.
        if (candidates->data[i].logit <= 0) {
            candidates->data[i].logit *= penalty;
        } else {
            candidates->data[i].logit /= penalty;
        }

        candidates->data[i].logit -= presence_penalty;
    }

    candidates->sorted = false;

}

void sample_temperature(llama_token_data_array * candidates_p, float temp, float smoothing_factor)
{
    if (temp <= 0)
    {
        // Imitate greedy sampling
        temp = 0.00390625f; //cannot be zero else div0, this is 1/256
        llama_sample_temp(nullptr, candidates_p, temp, 0);
        llama_sample_top_k(nullptr, candidates_p, 1, 1); //only want first candidate
    }
    else
    {
        llama_sample_temp(nullptr, candidates_p, temp, smoothing_factor);
    }
}

void sample_grammar(FileFormat file_format, int32_t n_vocab, llama_token_data_array * candidates, const struct llama_grammar * grammar) {

    const int64_t t_start_sample_us = ggml_time_us();

    bool allow_eos = false;
    for (const auto & stack : grammar->stacks) {
        if (stack.empty()) {
            allow_eos = true;
            break;
        }
    }

    const llama_token eos = GetEosID(file_format,n_vocab);
    const llama_token eot = GetEotID(file_format);

    std::vector<std::pair<std::vector<uint32_t>, llama_partial_utf8>> candidates_decoded;
    std::vector<llama_grammar_candidate>                              candidates_grammar;

    for (size_t i = 0; i < candidates->size; ++i) {
        const llama_token id    = candidates->data[i].id;
        const std::string piece = FileFormatTokenizeID(id,file_format);
        if (id == eos || (id==eot && id!=-1)) {
            if (!allow_eos) {
                candidates->data[i].logit = -INFINITY;
            }
        } else if (piece.empty() || piece[0] == 0) {
            candidates->data[i].logit = -INFINITY;
        } else {
            candidates_decoded.push_back(decode_utf8(piece.c_str(), grammar->partial_utf8));
            candidates_grammar.push_back({ i, candidates_decoded.back().first.data(), candidates_decoded.back().second });
        }
    }

    const auto rejects = llama_grammar_reject_candidates(grammar->rules, grammar->stacks, candidates_grammar);
    for (const auto & reject : rejects) {
        candidates->data[reject.index].logit = -INFINITY;
    }

}

int SampleLogits(const float * logits, int n_ctx, int n_vocab, int rep_pen_range, float rep_pen, float rep_pen_slope, float presence_penalty, float top_k, float top_a, float top_p, float min_p, float typical_p, float tfs, float temp, std::mt19937 & rng,
int mirostat, float mirostat_tau, float mirostat_eta, float dry_multiplier, float dry_base, int dry_allowed_length, int dry_penalty_last_n, const std::vector<samplers> & sampler_order, llama_grammar * grammar, float dynatemp_range, float dynatemp_exponent, float smoothing_factor)
{
    int id = 0;
    std::vector<llama_token_data> candidates;
    candidates.reserve(n_vocab);
    for (llama_token token_id = 0; token_id < n_vocab; token_id++) {
        candidates.emplace_back(llama_token_data{token_id, logits[token_id], 0.0f});
    }

    for(int i=0;i<logit_biases.size();++i)
    {
        auto & itm = logit_biases[i];
        candidates[itm.token_id].logit += itm.bias;
    }

    llama_token_data_array candidates_p = { candidates.data(), candidates.size(), false };

    if (grammar != nullptr) {
        sample_grammar(file_format, n_vocab, &candidates_p, grammar);
    }

    if (mirostat == 1 || mirostat == 2)
    {
        static float mirostat_mu = 2.0f * mirostat_tau;
        const int mirostat_m = 100;
        sample_rep_pen(n_ctx, rep_pen_range, rep_pen, rep_pen_slope, presence_penalty, &candidates_p);
        sample_temperature(&candidates_p, temp, smoothing_factor);
        if (mirostat == 1)
        {
            id = sample_token_mirostat(n_vocab, &candidates_p, rng, mirostat_tau, mirostat_eta, mirostat_m, &mirostat_mu);
        }
        else
        {
            id = sample_token_mirostat_v2(&candidates_p, rng, mirostat_tau, mirostat_eta, &mirostat_mu);
        }
    }
    else
    {
        for (int i = 0; i < sampler_order.size(); i++)
        {
            switch (sampler_order[i])
            {
                case KCPP_SAMPLER_TOP_K:
                    llama_sample_top_k(nullptr, &candidates_p, top_k,1);
                    break;
                case KCPP_SAMPLER_TOP_A:
                    sample_top_a(&candidates_p,top_a,1);
                    break;
                case KCPP_SAMPLER_TOP_P:
                    llama_sample_top_p(nullptr, &candidates_p, top_p,1);
                    llama_sample_min_p(nullptr, &candidates_p, min_p,1);
                    break;
                case KCPP_SAMPLER_TFS:
                    llama_sample_tail_free(nullptr, &candidates_p, tfs,1);
                    break;
                case KCPP_SAMPLER_TYP:
                    llama_sample_typical(nullptr, &candidates_p, typical_p,1);
                    break;
                case KCPP_SAMPLER_TEMP:
                    if (dynatemp_range>0)
                    {
                        float dynatemp_min = temp - dynatemp_range;
                        float dynatemp_max = temp + dynatemp_range;
                        //do not allow negative values
                        dynatemp_min = dynatemp_min<0?0:dynatemp_min;
                        dynatemp_max = dynatemp_max<0?0:dynatemp_max;
                        dynatemp_exponent = dynatemp_exponent<0?0:dynatemp_exponent;
                        llama_sample_entropy(nullptr, &candidates_p, dynatemp_min, dynatemp_max, dynatemp_exponent, smoothing_factor);
                    }
                    else
                    {
                        sample_temperature(&candidates_p, temp, smoothing_factor);
                    }
                    break;
                case KCPP_SAMPLER_REP_PEN:
                    sample_rep_pen(n_ctx, rep_pen_range, rep_pen, rep_pen_slope, presence_penalty, &candidates_p);
                    sample_dry(n_ctx, dry_penalty_last_n, dry_multiplier, dry_base, dry_allowed_length, dry_sequence_breakers, &candidates_p);
                    break;
                default:
                    printf("\nSampleLogits: Unknown Sampler : %d",sampler_order[i]);
                    break;
            }
        }
        id = sample_token(&candidates_p, rng);
    }

    return id;
}


static void grammar_accept_token(FileFormat file_format, int32_t n_vocab, struct llama_grammar * grammar, llama_token token)
{
    if (token == GetEosID(file_format,n_vocab) || (token!=-1 && token == GetEotID(file_format))) {
        for (const auto & stack : grammar->stacks) {
            if (stack.empty()) {
                return;
            }
        }
        GGML_ASSERT(false);
    }
    const std::string piece = FileFormatTokenizeID(token,file_format);

    // Note terminating 0 in decoded string
    const auto   decoded     = decode_utf8(piece.c_str(), grammar->partial_utf8);
    const auto & code_points = decoded.first;
    for (auto it = code_points.begin(), end = code_points.end() - 1; it != end; ++it) {
        auto prev_stacks = grammar->stacks;
        llama_grammar_accept(grammar->rules, prev_stacks, *it, grammar->stacks);
    }
    grammar->partial_utf8 = decoded.second;
    GGML_ASSERT(!grammar->stacks.empty());
}

static void load_grammar(const std::string & gammarstr)
{
    if(grammar!=nullptr) //on demand free when next grammar is loaded
    {
        llama_grammar_free(grammar);
        grammar = nullptr;
    }

    if (!gammarstr.empty()) {
        parsed_grammar = grammar_parser::parse(gammarstr.c_str());
        // will be empty (default) if there are parse errors
        if (parsed_grammar.rules.empty()) {
            printf("\nIgnored invalid grammar sampler.");
            return;
        }
        if(debugmode==1)
        {
            grammar_parser::print_grammar(stderr, parsed_grammar);
        }
        std::vector<const llama_grammar_element *> grammar_rules(parsed_grammar.c_rules());
        grammar = llama_grammar_init(grammar_rules.data(), grammar_rules.size(), parsed_grammar.symbol_ids.at("root"));
    }
}

static bool kcpp_eval_image(llama_context * ctx_llama, float * img_embd, int num_img_tokens, int n_batch, int * n_past) {
    int n_embd  = llama_n_embd(llama_get_model(ctx_llama));

    for (int i = 0; i < num_img_tokens; i += n_batch) {
        int n_eval = num_img_tokens - i;
        if (n_eval > n_batch) {
            n_eval = n_batch;
        }
        llama_batch batch = {int32_t(n_eval), nullptr, (img_embd+i*n_embd), nullptr, nullptr, nullptr, nullptr, *n_past, 1, 0, };
        if (llama_decode(ctx_llama, batch)) {
            fprintf(stderr, "\n%s : failed to eval image\n", __func__);
            return false;
        }
        *n_past += n_eval;
    }
    return true;
}

//given an old GGUF context and a new context that has some middle portion removed,
//find and remove the middle portion from the old context from the KV. Does not fast forward after this destructive action
void PurgeMissingTokens(llama_context * ctx, std::vector<int> &current_context_tokens, std::vector<int> &new_context_tokens, const int genamt, const int nctx)
{
    //scan from start old and new ctx, until first mismatch found, save as p0
    //check remaining old and new ctx for longest common subseq, which needs to be at 256 tokens
    //test: longest common subseq (LCQ) MUST start within 0 tokens from end of memory, otherwise purge fails
    //if passed, save beginning of LCQ from old ctx as p1
    //remove all tokens from old ctx between p0 and p1, updating both arrays and kv, then continue as normal

    const int ShortfallThreshold = 200 + (nctx/30); //dont trigger shifting if the distance between trimstart and currhead < this
    const int SlackAllowance = 60 + (nctx/50); //in case the end text is slightly modified, be forgiving

    int trimstart = 0;
    int new_tokens_len = new_context_tokens.size();
    bool purgeneeded = true;

    for (int i = 0; i < current_context_tokens.size(); ++i)
    {
        if (current_context_tokens[i] == new_context_tokens[i])
        {
            trimstart += 1;
        }
        else
        {
            break;
        }
        if ((i + 2) >= new_tokens_len)
        {
            purgeneeded = false;
            break; //no surgery required
        }
    }

    if(!purgeneeded || new_tokens_len < 6 || current_context_tokens.size() < 6 || new_tokens_len - trimstart < ShortfallThreshold)
    {
        return; //no purge is needed
    }

    //at least this many tokens need to match, otherwise don't bother trimming
    const int LCSTokThreshold = std::max(std::min((new_tokens_len - trimstart) - (genamt+SlackAllowance), (int)(nctx*0.45)), ShortfallThreshold-SlackAllowance);

    auto curr_ctx_without_memory = std::vector<int>(current_context_tokens.begin() + trimstart, current_context_tokens.end());
    auto new_ctx_without_memory = std::vector<int>(new_context_tokens.begin() + trimstart, new_context_tokens.end());

    auto shared = LongestCommonSubseq(curr_ctx_without_memory, new_ctx_without_memory);

    if (shared.size() > LCSTokThreshold && ArrStartWith(new_ctx_without_memory, shared)) // enough tokens in common
    {
        int found = ArrFindIndexOf(current_context_tokens,shared);
        if(found>=0 && found > trimstart)
        {

            //extract the unwanted tokens out from context and KV
            int diff = found - trimstart;
            llama_kv_cache_seq_rm(llama_ctx_v4, 0, trimstart, trimstart + diff);
            llama_kv_cache_seq_add(llama_ctx_v4, 0, trimstart + diff, -1, -diff);

            for (size_t i = trimstart + diff; i < current_context_tokens.size() - 1; i++)
            {
                current_context_tokens[i - diff] = current_context_tokens[i];
            }

            printf("\n[Context Shifting: Erased %d tokens at position %d]", diff, trimstart + 1);

            current_context_tokens.resize(current_context_tokens.size() - diff);
        }
    }

}

static int GetBatchSize(int desiredBlasBatchSize,FileFormat in_file_format)
{
    //check if approved to use BLAS
    bool approved_format = !(file_format == FileFormat::BADFORMAT ||
                            file_format == FileFormat::GPT2_1 ||
                            file_format == FileFormat::GPTJ_1 ||
                            file_format == FileFormat::GPTJ_2 ||
                            file_format == FileFormat::RWKV_1 ||
                            file_format==FileFormat::RWKV_2);
    if(!approved_format || desiredBlasBatchSize<=0)
    {
        desiredBlasBatchSize = 16;
    }
    if (file_format != FileFormat::GGML && file_format != FileFormat::GGHF && file_format != FileFormat::GGJT && file_format != FileFormat::GGJT_2 && file_format != FileFormat::GGJT_3 && file_format != FileFormat::GGUF_GENERIC)
    {
        desiredBlasBatchSize = (desiredBlasBatchSize > 256 ? 256 : desiredBlasBatchSize);
    }
    if (file_format == FileFormat::RWKV_1 || file_format==FileFormat::RWKV_2)
    {
        desiredBlasBatchSize = 1;
    }
    return desiredBlasBatchSize;
}

//this function applies automatic scaling to rope freq base when the desired context exceeds trained context
static float CalcGradientAIRopeFreqBase(float original_rope_base, int n_ctx_train, int n_ctx_desired, GGUFArch model_arch)
{
    if(n_ctx_desired <= n_ctx_train || n_ctx_desired <= 2048)
    {
        return original_rope_base;
    }
	else
	{
        float ctx_multiplier = (model_arch==GGUFArch::ARCH_SOLAR?8.0f:1.0f);
        float chi_ctx_train_value = (n_ctx_train * ctx_multiplier) / 6.28318;
        float chi_ctx_value = (n_ctx_desired * ctx_multiplier) / 6.28318;
        float gradient_ai_rope_freq_base_value = powf(original_rope_base, log10f(chi_ctx_value) / log10f(chi_ctx_train_value));

        if(debugmode==1)
        {
            printf("Trained max context length (value:%.d).\n", n_ctx_train);
            printf("Desired context length (value:%.d).\n", n_ctx_desired);
            printf("Solar context multiplier (value:%.3f).\n", ctx_multiplier);
            printf("Chi context train (value:%.3f).\n", chi_ctx_train_value);
            printf("Chi chosen context (value:%.3f).\n", chi_ctx_value);
            printf("Log Chi context train (value:%.3f).\n", log10f(chi_ctx_train_value));
            printf("Log Chi chosen context (value:%.3f).\n", log10f(chi_ctx_value));
            printf("RoPE Frequency Base value (value:%.3f).\n", original_rope_base);
            printf("RoPE base calculated via Gradient AI formula. (value:%.1f).\n", gradient_ai_rope_freq_base_value);
        }

	    if(model_arch==GGUFArch::ARCH_SOLAR)
        {
            float extended_rope_positive_offset_value = 1 + ((log10f(chi_ctx_value) - log10f(chi_ctx_train_value)) / ((log10f(chi_ctx_value) * log10f(chi_ctx_train_value)) - (log10f(chi_ctx_value) + log10f(chi_ctx_train_value))));
            float rope_freq_base_with_positive_offset = gradient_ai_rope_freq_base_value * extended_rope_positive_offset_value;
            if(debugmode==1)
            {
                printf("Extended RoPE Positive Offset (multiplicator) for Solar based models. (value:%.3f).\n", extended_rope_positive_offset_value);
                printf("RoPE base calculated via Gradient AI formula for Solar based models. (value:%.1f).\n", rope_freq_base_with_positive_offset);
            }
            return rope_freq_base_with_positive_offset;
        }
        else
        {
	        return gradient_ai_rope_freq_base_value;
        }
    }
}

ModelLoadResult gpttype_load_model(const load_model_inputs inputs, FileFormat in_file_format, FileFormatExtraMeta in_file_format_meta)
{
    ggml_time_init();
    kcpp_params = new gpt_params(); //allocate on heap to avoid linux segfault. yes this leaks memory.

    file_format = in_file_format;
    file_format_meta = in_file_format_meta;
    kcpp_params->n_threads = inputs.threads;
    kcpp_params->n_threads_batch = inputs.blasthreads;
    bool isGguf = (file_format == FileFormat::GGUF_GENERIC);
    kcpp_params->n_batch = GetBatchSize(inputs.blasbatchsize, in_file_format);
    kcpp_params->n_ubatch = kcpp_params->n_batch;
    kcpp_params->flash_attn = inputs.flash_attention;
    modelname = kcpp_params->model = inputs.model_filename;
    useSmartContext = inputs.use_smartcontext;
    useContextShift = inputs.use_contextshift;
    debugmode = inputs.debugmode;

    auto clamped_max_context_length = inputs.max_context_length;

    if(clamped_max_context_length>16384 &&
    file_format != FileFormat::GGUF_GENERIC)
    {
        printf("Warning: Only GGUF models can use max context above 16k. Max context lowered to 16k.\n");
        clamped_max_context_length = 16384;
    }

    kcpp_params->n_ctx = clamped_max_context_length;
    max_context_limit_at_load = clamped_max_context_length;

    neox_ctx_v2.hparams.n_ctx  = neox_ctx_v3.hparams.n_ctx
    = gptj_ctx_v1.hparams.n_ctx = gptj_ctx_v2.hparams.n_ctx = gptj_ctx_v3.hparams.n_ctx
    = gpt2_ctx_v1.hparams.n_ctx = gpt2_ctx_v2.hparams.n_ctx = gpt2_ctx_v3.hparams.n_ctx
    = mpt_ctx_v3.hparams.n_ctx = kcpp_params->n_ctx;

    //determine rope scaling params
    float rope_freq_scale = 1.0f;
    float rope_freq_base = 10000.0f;
    bool overwriteRope = false;
    if(inputs.rope_freq_scale>0.0f)
    {
        rope_freq_scale = inputs.rope_freq_scale;
        rope_freq_base = inputs.rope_freq_base;
        overwriteRope = true;
        printf("Using Custom RoPE scaling (scale:%.3f, base:%.1f).\n",rope_freq_scale,rope_freq_base);
    }
    else
    {
        //Set freq base for all, including non GGUF. If we are using GGUF, this will be overwritten with more accurate values later.
        rope_freq_base = CalcGradientAIRopeFreqBase(10000.0f,2048,kcpp_params->n_ctx, GGUFArch::ARCH_DEFAULT);
        if(file_format==FileFormat::GGUF_GENERIC)
        {
            printf("Using automatic RoPE scaling for GGUF. If the model has custom RoPE settings, they'll be used directly instead!\n");
            printf("It means that the RoPE values written above will be replaced by the RoPE values indicated after loading.\n");
        }
        else
        {
            printf("Using Automatic RoPE scaling, Pre-GGUF (scale:%.3f, base:%.1f).\n",rope_freq_scale, rope_freq_base);
        }
    }
    gptj_ctx_v3.hparams.rope_freq_scale = neox_ctx_v3.hparams.rope_freq_scale = rope_freq_scale;
    gptj_ctx_v3.hparams.rope_freq_base = neox_ctx_v3.hparams.rope_freq_base = rope_freq_base;

    //this is used for the mem_per_token eval, openblas needs more RAM
    bool v3_use_scratch = ggml_v3_cpu_has_gpublas();

    int cu_parseinfo_maindevice = inputs.cublas_info<=0?0:inputs.cublas_info;

    printf("System Info: %s\n", llama_print_system_info());
    #if defined(GGML_USE_CUDA)
    if(file_format!=FileFormat::GGUF_GENERIC)
    {
        if(ggml_v3_cpu_has_gpublas() && cu_parseinfo_maindevice>0)
        {
            printf("CUBLAS v3: Set main device to %d\n",cu_parseinfo_maindevice);
            ggml_v3_cuda_set_main_device(cu_parseinfo_maindevice);
        }
    }

    #endif
    SetQuantsUnshuffled(false);
    if(file_format == FileFormat::GGML || file_format == FileFormat::GGHF || file_format == FileFormat::GGJT || file_format == FileFormat::GGJT_2)
    {
        //newer format has bit unshuffling
        SetQuantsUnshuffled(file_format == FileFormat::GGJT_2);
        llama_v2_context_params llama_ctx_params_v2 = llama_v2_context_default_params();
        llama_ctx_params_v2.n_ctx = clamped_max_context_length;
        llama_ctx_params_v2.seed = -1;
        llama_ctx_params_v2.f16_kv = true;
        llama_ctx_params_v2.logits_all = false;
        llama_ctx_params_v2.use_mmap = inputs.use_mmap;
        llama_ctx_params_v2.use_mlock = inputs.use_mlock;
        llama_ctx_params_v2.n_gpu_layers = inputs.gpulayers;

        llama_ctx_v2 = llama_v2_init_from_file(modelname.c_str(), llama_ctx_params_v2);

        if (llama_ctx_v2 == NULL)
        {
            fprintf(stderr, "%s: error: failed to load model '%s'\n", __func__, modelname.c_str());
            return ModelLoadResult::FAIL;
        }

        printf("\n---\nWarning: Your model may be an OUTDATED format (ver %d). Please reconvert it for better results!\n---\n", file_format);

        if (lora_filename != "")
        {
            printf("\nAttempting to apply LORA adapter: %s\n", lora_filename.c_str());

            const char * lora_base_arg = NULL;
            if (lora_base != "") {
                printf("Using LORA base model: %s\n", lora_base.c_str());
                lora_base_arg = lora_base.c_str();
            }

            int err = llama_v2_apply_lora_from_file(llama_ctx_v2,
                                                 lora_filename.c_str(),
                                                 lora_base_arg,
                                                 kcpp_params->n_threads);
            if (err != 0)
            {
                fprintf(stderr, "%s: error: failed to apply lora adapter\n", __func__);
                return ModelLoadResult::FAIL;
            }
        }

        n_vocab = llama_v2_n_vocab(llama_ctx_v2);

        //determine mem per token
        const std::vector<int> tmp = {1, 2, 3, 4};
        llama_v2_eval(llama_ctx_v2, tmp.data(), tmp.size(), 0, kcpp_params->n_threads);
        return ModelLoadResult::SUCCESS;
    }
    else if(file_format == FileFormat::GGJT_3)
    {
        llama_v3_context_params llama_ctx_params = llama_v3_context_default_params();
        llama_ctx_params.n_ctx = clamped_max_context_length;
        llama_ctx_params.seed = -1;
        llama_ctx_params.f16_kv = true;
        llama_ctx_params.low_vram = inputs.low_vram;
        llama_ctx_params.mul_mat_q = inputs.use_mmq;
        llama_ctx_params.logits_all = false;
        llama_ctx_params.use_mmap = inputs.use_mmap;
        llama_ctx_params.use_mlock = inputs.use_mlock;
        llama_ctx_params.n_gpu_layers = inputs.gpulayers;
        llama_ctx_params.main_gpu = cu_parseinfo_maindevice;
        llama_ctx_params.rope_freq_base = rope_freq_base;
        llama_ctx_params.rope_freq_scale = rope_freq_scale;
        llama_ctx_params.n_batch = kcpp_params->n_batch;

        #if defined(GGML_USE_CUDA) || defined(GGML_USE_VULKAN)
        bool ts_all_zero = true;
        for (int i = 0; i < tensor_split_max; ++i) {
            if (inputs.tensor_split[i] != 0.0f) {
                ts_all_zero = false;
                break;
            }
        }
        if(!ts_all_zero)
        {
            printf("\nApplying Tensor Split...");
            llama_ctx_params.tensor_split = inputs.tensor_split;
        }
        #endif

        llama_ctx_v3 = llama_v3_init_from_file(modelname.c_str(), llama_ctx_params);

        if (llama_ctx_v3 == NULL)
        {
            fprintf(stderr, "%s: error: failed to load model '%s'\n", __func__, modelname.c_str());
            return ModelLoadResult::FAIL;
        }
        if (lora_filename != "")
        {
            printf("\nAttempting to apply LORA adapter: %s\n", lora_filename.c_str());

            const char * lora_base_arg = NULL;
            if (lora_base != "") {
                printf("Using LORA base model: %s\n", lora_base.c_str());
                lora_base_arg = lora_base.c_str();
            }

            int err = llama_v3_apply_lora_from_file(llama_ctx_v3,
                                                 lora_filename.c_str(),
                                                 lora_base_arg,
                                                 kcpp_params->n_threads);
            if (err != 0)
            {
                fprintf(stderr, "%s: error: failed to apply lora adapter\n", __func__);
                return ModelLoadResult::FAIL;
            }
        }

        n_vocab = llama_v3_n_vocab(llama_ctx_v3);

        //determine mem per token
        const std::vector<int> tmp = {1, 2, 3, 4};
        auto er = llama_v3_eval(llama_ctx_v3, tmp.data(), tmp.size(), 0, kcpp_params->n_threads);
        if(er!=0)
        {
            printf("\nLLAMA EVAL returned nonzero!\n");
        }
        return ModelLoadResult::SUCCESS;
    }
    else if(file_format==FileFormat::GGUF_GENERIC)
    {
        llama_backend_init();

        llama_model_params model_params = llama_model_default_params();
        llama_context_params llama_ctx_params = llama_context_default_params();
        llama_ctx_params.n_ctx = clamped_max_context_length;
        if(useContextShift)
        {
           llama_ctx_params.n_ctx += extra_context_handle_fragmentation;
        }

        llama_ctx_params.seed = -1;
        llama_ctx_params.offload_kqv = !inputs.low_vram;
        llama_ctx_params.logits_all = false;
        model_params.use_mmap = inputs.use_mmap;
        model_params.use_mlock = inputs.use_mlock;
        model_params.n_gpu_layers = inputs.gpulayers;

        #if defined(GGML_USE_CLBLAST)
        if(file_format==FileFormat::GGUF_GENERIC && model_params.n_gpu_layers>0)
        {
            if(file_format_meta.model_architecture == GGUFArch::ARCH_FALCON)
            {
                printf("\nOpenCL does not support GPU Layer offloading for this model architecture! GPU Offload has been disabled.\n");
                model_params.n_gpu_layers = 0;
            }
            else if(file_format_meta.n_expert_count>1)
            {
                printf("\nOpenCL cannot use regular GPU offloading for this model architecture. A fallback GPU offloader will be used with degraded performance.\n");
                clblast_offload_fallback_mode = true;
            }
        }
        #endif
        #if defined(GGML_USE_CUDA)
        if(ggml_cpu_has_gpublas() && cu_parseinfo_maindevice>0)
        {
            printf("CUBLAS: Set main device to %d\n",cu_parseinfo_maindevice);
        }
        ggml_cuda_set_mul_mat_q(inputs.use_mmq);
        if(file_format_meta.model_architecture == GGUFArch::ARCH_QWEN2 && kcpp_params->flash_attn)
        {
            printf("CUBLAS: Warning, you are running Qwen2 without Flash Attention and may observe incoherent output.\n");
        }
        #endif
        model_params.main_gpu = cu_parseinfo_maindevice;

        #if defined(GGML_USE_CUDA)
        model_params.split_mode = (inputs.use_rowsplit?llama_split_mode::LLAMA_SPLIT_MODE_ROW:llama_split_mode::LLAMA_SPLIT_MODE_LAYER);
        #else
        model_params.split_mode = llama_split_mode::LLAMA_SPLIT_MODE_LAYER;
        #endif

        llama_ctx_params.n_batch = kcpp_params->n_batch;
        llama_ctx_params.n_ubatch = kcpp_params->n_ubatch;
        llama_ctx_params.n_threads = kcpp_params->n_threads;
        llama_ctx_params.n_threads_batch = kcpp_params->n_threads_batch;

        #if defined(GGML_USE_CUDA) || defined(GGML_USE_VULKAN)
        bool ts_all_zero = true;
        for (int i = 0; i < tensor_split_max; ++i) {
            if (inputs.tensor_split[i] != 0.0f) {
                ts_all_zero = false;
                break;
            }
        }
        if(!ts_all_zero)
        {
            printf("\nApplying Tensor Split...");
            model_params.tensor_split = inputs.tensor_split;
        }
        #endif

        //compat for old falcon
        if(file_format_meta.fileversion==1)
        {
            //apply compat fix
            printf("\nUsing older tokenizer for GGUFv1...");
            OldBPETokenizerMode = true;
        }

        llama_model * llamamodel = llama_load_model_from_file(modelname.c_str(), model_params);
        if(overwriteRope)
        {
            llama_ctx_params.rope_freq_base = rope_freq_base;
            llama_ctx_params.rope_freq_scale = rope_freq_scale;
        }
        else
        {
            //if the model modifes rope in any way, or uses yarn, use the model values. Otherwise, use our automatic ones
            //special exception for llama, which uses auto scale
            if((llamamodel->hparams.rope_freq_base_train!=10000.0f && llamamodel->hparams.rope_freq_base_train!=500000.0f) ||
            llamamodel->hparams.rope_freq_scale_train!=1.0f ||
            llamamodel->hparams.rope_scaling_type_train==2)
            {
                printf("Automatic RoPE Scaling: Using model internal value.\n");
            }
            else
            {
				//Calculate rope_freq_base using the gradientAI formula, solar requires ctx *8 for correct scaling
                rope_freq_base = CalcGradientAIRopeFreqBase(llamamodel->hparams.rope_freq_base_train, file_format_meta.n_ctx_train, kcpp_params->n_ctx, file_format_meta.model_architecture);
                llama_ctx_params.rope_freq_base = rope_freq_base;
                llama_ctx_params.rope_freq_scale = rope_freq_scale;
                printf("Automatic RoPE Scaling: Using (scale:%.3f, base:%.1f).\n", rope_freq_scale, rope_freq_base);
            }
        }

        llama_ctx_params.flash_attn = kcpp_params->flash_attn;
        llama_ctx_params.type_k = (inputs.quant_k>1?GGML_TYPE_Q4_0:(inputs.quant_k==1?GGML_TYPE_Q8_0:GGML_TYPE_F16));
        llama_ctx_params.type_v = (inputs.quant_v>1?GGML_TYPE_Q4_0:(inputs.quant_v==1?GGML_TYPE_Q8_0:GGML_TYPE_F16));
        llama_ctx_v4 = llama_new_context_with_model(llamamodel, llama_ctx_params);

        if (llama_ctx_v4 == NULL)
        {
            fprintf(stderr, "%s: error: failed to load model '%s'\n", __func__, modelname.c_str());
            return ModelLoadResult::FAIL;
        }
        if (lora_filename != "")
        {
            printf("\nAttempting to apply LORA adapter: %s\n", lora_filename.c_str());

            const char * lora_base_arg = NULL;
            if (lora_base != "") {
                printf("Using LORA base model: %s\n", lora_base.c_str());
                lora_base_arg = lora_base.c_str();
            }

            int err = llama_model_apply_lora_from_file(llamamodel,
                                                       lora_filename.c_str(),
                                                       1.0f,
                                                       lora_base_arg,
                                                       kcpp_params->n_threads);
            if (err != 0)
            {
                fprintf(stderr, "%s: error: failed to apply lora adapter\n", __func__);
                return ModelLoadResult::FAIL;
            }
        }

        if(mmproj_filename != "" && file_format==FileFormat::GGUF_GENERIC)
        {
            printf("\nAttempting to apply Multimodal Projector: %s\n", mmproj_filename.c_str());
            clp_ctx = clip_model_load(mmproj_filename.c_str(), /*verbosity=*/ 1);
            if(clp_ctx == nullptr) {
                fprintf(stderr, "%s: error: failed to load mmproj model!\n", __func__);
                return ModelLoadResult::FAIL;
            }
            const int n_embd_clip = clip_n_mmproj_embd(clp_ctx);
            const int n_embd_llm  = llama_n_embd(llamamodel);
            if (n_embd_clip != n_embd_llm) {
                fprintf(stderr, "%s: mmproj embedding mismatch (%d and %d)! Make sure you use the correct mmproj file!\n", __func__,n_embd_clip, n_embd_llm);
                return ModelLoadResult::FAIL;
            }
            clp_img_data = clip_image_u8_init();
        }

        n_vocab = llama_n_vocab(llamamodel);

        //determine mem per token
        std::vector<int> tmp = {1, 2, 3, 4};
        llama_kv_cache_clear(llama_ctx_v4);
        auto er = llama_decode(llama_ctx_v4, llama_batch_get_one(tmp.data(), tmp.size(), 0, 0));
        if(er!=0)
        {
            printf("\nLLAMA EVAL returned nonzero: %d\n",er);
        }
        return ModelLoadResult::SUCCESS;
    }
    else if (file_format == FileFormat::RWKV_1 || file_format==FileFormat::RWKV_2)
    {
        //start loading the models first
        bool useWorldTokenizer = false;
        if (file_format == FileFormat::RWKV_1)
        {
            rwkv_ctx_v2 = rwkv_v2_init_from_file(modelname.c_str(), kcpp_params->n_threads);
        }
        else //rwkv_2
        {
            rwkv_ctx_v3 = rwkv_init_from_file(modelname.c_str(), kcpp_params->n_threads);

            if(inputs.gpulayers>0)
            {
                rwkv_gpu_offload_layers(rwkv_ctx_v3,inputs.gpulayers);
            }

            const struct rwkv_file_header & header = rwkv_ctx_v3->instance->model.header;
            const size_t n_vocab = header.n_vocab;
            printf("\nDetected Vocab: %zu",n_vocab);
            if(n_vocab>60000)
            {
                printf("\nUsing WORLD TOKENIZER");
                useWorldTokenizer = true;
            }
        }

        std::string word;
        if(useWorldTokenizer)
        {
            read_rwkv_world_vocab();
        }
        else
        {
            read_rwkv_vocab();
        }

        int vocabsiz = rwkv_vocab.size();
        for (int i = 0; i < vocabsiz; i++)
        {
            uint32_t len;
            word = rwkv_vocab[i];
            vocab.token_to_id[word] = i;
            vocab.id_to_token[i] = word;
        }
        printf("\nRWKV Vocab: %u\n", vocabsiz);
        logits.resize(vocabsiz);

        n_vocab = vocab.id_to_token.size(); //handled seperately

        if (file_format == FileFormat::RWKV_1)
        {

            //setup buffers for rwkv state
            auto padding = 512u;
            auto statebufsiz = rwkv_v2_get_state_buffer_element_count(rwkv_ctx_v2) * sizeof(float) + padding;
            auto logitbufsiz = rwkv_v2_get_logits_buffer_element_count(rwkv_ctx_v2) * sizeof(float) + padding;

            printf("\nRWKV old Init: State Buffer:%lu, Logit Buffer:%lu\n", statebufsiz, logitbufsiz);
            rwkv_ctx_v2->state_out = (float *)malloc(statebufsiz);
            rwkv_ctx_v2->logits_out = (float *)malloc(logitbufsiz);
            rwkv_ctx_v2->state_in = nullptr;

            bool testeval = rwkv_v2_eval(rwkv_ctx_v2, 0, rwkv_ctx_v2->state_in, rwkv_ctx_v2->state_out, rwkv_ctx_v2->logits_out);
            if (!testeval)
            {
                printf("\nError: RWKV old Init Eval Failed!\n");
            }

            memcpy(logits.data(), rwkv_ctx_v2->logits_out, sizeof(float) * vocabsiz);

            if (rwkv_ctx_v2 == NULL)
            {
                return ModelLoadResult::FAIL;
            }
            return ModelLoadResult::SUCCESS;
        }
        else
        {
            //setup buffers for rwkv state
            auto padding = 512u;
            auto statebufsiz = rwkv_get_state_buffer_element_count(rwkv_ctx_v3) * sizeof(float) + padding;
            auto logitbufsiz = rwkv_get_logits_buffer_element_count(rwkv_ctx_v3) * sizeof(float) + padding;

            printf("\nRWKV Init: State Buffer:%lu, Logit Buffer:%lu\n", statebufsiz, logitbufsiz);
            rwkv_ctx_v3->state_out = (float *)malloc(statebufsiz);
            rwkv_ctx_v3->logits_out = (float *)malloc(logitbufsiz);
            rwkv_ctx_v3->state_in = nullptr;

            bool testeval = rwkv_eval(rwkv_ctx_v3, kcpp_params->n_threads, 0, rwkv_ctx_v3->state_in, rwkv_ctx_v3->state_out, rwkv_ctx_v3->logits_out);
            if (!testeval)
            {
                printf("\nError: RWKV Init Eval Failed!\n");
            }

            memcpy(logits.data(), rwkv_ctx_v3->logits_out, sizeof(float) * vocabsiz);

            if (rwkv_ctx_v3 == NULL)
            {
                return ModelLoadResult::FAIL;
            }
            return ModelLoadResult::SUCCESS;
        }
    }
    else if (file_format == FileFormat::GPT2_1)
    {
        ModelLoadResult res = legacy_gpt2_model_load(kcpp_params->model, gpt2_ctx_v1, vocab, file_format);
        if(res==ModelLoadResult::FAIL)
        {
            fprintf(stderr, "%s: failed to load model from '%s'\n", __func__, kcpp_params->model.c_str());
            return res;
        }
        else if(res==ModelLoadResult::RETRY_LOAD)
        {
            printf("\nTensor Transposition Detected! Retrying GPT-2 model loading...");
            return res;
        }

        n_vocab = gpt2_ctx_v1.hparams.n_vocab;

         // determine the required inference memory per token:
        legacy_gpt2_eval(gpt2_ctx_v1, kcpp_params->n_threads, 0, { 0, 1, 2, 3 }, logits, mem_per_token, file_format);
        return ModelLoadResult::SUCCESS;
    }
    else if (file_format == FileFormat::GPT2_2 || file_format==FileFormat::GPT2_3 || file_format==FileFormat::GPT2_4)
    {
        if(file_format==FileFormat::GPT2_4)
        {
            ModelLoadResult res = gpt2_model_load(kcpp_params->model, gpt2_ctx_v3, vocab, file_format, inputs.gpulayers);
            if(res==ModelLoadResult::FAIL)
            {
                fprintf(stderr, "%s: failed to load model from '%s'\n", __func__, kcpp_params->model.c_str());
                return res;
            }
            else if(res==ModelLoadResult::RETRY_LOAD)
            {
                printf("\nTensor Transposition Detected! Retrying GPT-2 model loading...");
                return res;
            }

            n_vocab = gpt2_ctx_v3.hparams.n_vocab;

            // determine the required inference memory per token:
            gpt2_eval(gpt2_ctx_v3, kcpp_params->n_threads, 0, { 0, 1, 2, 3 }, logits, mem_per_token, v3_use_scratch);
            return ModelLoadResult::SUCCESS;
        }
        else
        {
            //newer format has bit unshuffling
            SetQuantsUnshuffled(file_format == FileFormat::GPT2_3);

            ModelLoadResult res = gpt2_v2_model_load(kcpp_params->model, gpt2_ctx_v2, vocab, file_format, inputs.gpulayers);
            if(res==ModelLoadResult::FAIL)
            {
                fprintf(stderr, "%s: failed to load model from '%s'\n", __func__, kcpp_params->model.c_str());
                return res;
            }
            else if(res==ModelLoadResult::RETRY_LOAD)
            {
                printf("\nTensor Transposition Detected! Retrying GPT-2 model loading...");
                return res;
            }

            n_vocab = gpt2_ctx_v2.hparams.n_vocab;

            // determine the required inference memory per token:
            gpt2_v2_eval(gpt2_ctx_v2, kcpp_params->n_threads, 0, { 0, 1, 2, 3 }, logits, mem_per_token, file_format);
            return ModelLoadResult::SUCCESS;
        }
    }
    else if (file_format == FileFormat::GPTJ_1 || file_format == FileFormat::GPTJ_2)
    {
        ModelLoadResult res = legacy_gptj_model_load(kcpp_params->model, gptj_ctx_v1, vocab, file_format);
        if(res==ModelLoadResult::FAIL)
        {
            fprintf(stderr, "%s: failed to load model from '%s'\n", __func__, kcpp_params->model.c_str());
            return res;
        }
        else if(res==ModelLoadResult::RETRY_LOAD)
        {
            printf("\nTensor Transposition Detected! Retrying GPT-J model loading...");
            return res;
        }

        n_vocab = gptj_ctx_v1.hparams.n_vocab;

         // determine the required inference memory per token:
        legacy_gptj_eval(gptj_ctx_v1, kcpp_params->n_threads, 0, { 0, 1, 2, 3 }, logits, mem_per_token, file_format);

        //if the logits are NAN or duplicated, it means the model is incompatible
        if(logits.size()>0 && IsNanCheck(logits[0]))
        {
            printf("\nBad Logits detected! Retrying GPT-J model loading...");
            ggml_v1_free(gptj_ctx_v1.ctx);
            return ModelLoadResult::RETRY_LOAD;
        }

        return ModelLoadResult::SUCCESS;
    }
    else if(file_format == FileFormat::GPTJ_3 || file_format == FileFormat::GPTJ_4 || file_format == FileFormat::GPTJ_5)
    {
        if(file_format == FileFormat::GPTJ_5)
        {
            ModelLoadResult loadresult = gptj_model_load(kcpp_params->model, gptj_ctx_v3, vocab, inputs.gpulayers);
            if (loadresult == ModelLoadResult::FAIL)
            {
                fprintf(stderr, "%s: failed to load model from '%s'\n", __func__, kcpp_params->model.c_str());
                return loadresult;
            }
            else if (loadresult == ModelLoadResult::RETRY_LOAD)
            {
                printf("\nTensor Transposition Detected! Retrying GPT-J model loading...");
                return loadresult;
            }

            n_vocab = gptj_ctx_v3.hparams.n_vocab;

            // determine the required inference memory per token:
            gptj_eval(gptj_ctx_v3, kcpp_params->n_threads, 0, { 0, 1, 2, 3 }, logits, mem_per_token, v3_use_scratch);

            //if the logits are NAN or duplicated, it means the model is incompatible
            std::vector<float> oldlogits(logits);

            //this is another hack because they change the library - we run the eval through the model
            //twice and compare logits. if they give the same logits for different inputs, model is broken
            gptj_eval(gptj_ctx_v3, kcpp_params->n_threads, 0, {4, 5, 6, 7}, logits, mem_per_token, v3_use_scratch);

            if(logits.size()>0 && (IsNanCheck(logits[0]) || LogitsDuplicated(oldlogits,logits)))
            {
                printf("\nBad Logits detected! Retrying GPT-J model loading...");
                ggml_v3_free(gptj_ctx_v3.ctx);
                return ModelLoadResult::RETRY_LOAD;
            }

            return ModelLoadResult::SUCCESS;
        }
        else
        {
            //newer format has bit unshuffling
            SetQuantsUnshuffled(file_format == FileFormat::GPTJ_4);

            ModelLoadResult loadresult = gptj_v2_model_load(kcpp_params->model, gptj_ctx_v2, vocab, inputs.gpulayers);
            if (loadresult == ModelLoadResult::FAIL)
            {
                fprintf(stderr, "%s: failed to load model from '%s'\n", __func__, kcpp_params->model.c_str());
                return loadresult;
            }
            else if (loadresult == ModelLoadResult::RETRY_LOAD)
            {
                printf("\nTensor Transposition Detected! Retrying GPT-J model loading...");
                return loadresult;
            }

            n_vocab = gptj_ctx_v2.hparams.n_vocab;

            // determine the required inference memory per token:
            gptj_v2_eval(gptj_ctx_v2, kcpp_params->n_threads, 0, { 0, 1, 2, 3 }, logits, mem_per_token);

            //if the logits are NAN or duplicated, it means the model is incompatible
            std::vector<float> oldlogits(logits);

            //this is another hack because they change the library - we run the eval through the model
            //twice and compare logits. if they give the same logits for different inputs, model is broken
            gptj_v2_eval(gptj_ctx_v2, kcpp_params->n_threads, 0, {4, 5, 6, 7}, logits, mem_per_token);

            if(logits.size()>0 && (IsNanCheck(logits[0]) || LogitsDuplicated(oldlogits,logits)))
            {
                printf("\nBad Logits detected! Retrying GPT-J model loading...");
                ggml_v2_free(gptj_ctx_v2.ctx);
                return ModelLoadResult::RETRY_LOAD;
            }

            return ModelLoadResult::SUCCESS;
        }
    }
    else if(file_format==FileFormat::NEOX_1 || file_format==FileFormat::NEOX_2 || file_format==FileFormat::NEOX_3 || file_format==FileFormat::NEOX_4 || file_format==FileFormat::NEOX_5|| file_format==FileFormat::NEOX_6|| file_format==FileFormat::NEOX_7)
    {
        if(file_format==FileFormat::NEOX_6|| file_format==FileFormat::NEOX_7)
        {
            ModelLoadResult res = gpt_neox_model_load(kcpp_params->model, neox_ctx_v3, vocab, file_format, inputs.gpulayers);
            if(res==ModelLoadResult::FAIL)
            {
                fprintf(stderr, "%s: failed to load model from '%s'\n", __func__, kcpp_params->model.c_str());
                return res;
            }
            else if(res==ModelLoadResult::RETRY_LOAD)
            {
                printf("\nIncorrect Tensor Size Detected! Retrying GPT-NeoX model loading...");
                return res;
            }

            n_vocab = neox_ctx_v3.hparams.n_vocab;

            // determine the required inference memory per token:
            gpt_neox_eval(neox_ctx_v3, kcpp_params->n_threads, 0, { 0, 1, 2, 3 }, logits, mem_per_token, v3_use_scratch);

            return ModelLoadResult::SUCCESS;
        }
        else
        {
            //newer format has bit unshuffling
            SetQuantsUnshuffled(file_format==FileFormat::NEOX_4 || file_format==FileFormat::NEOX_5);

            ModelLoadResult res = gpt_neox_v2_model_load(kcpp_params->model, neox_ctx_v2, vocab, file_format);
            if(res==ModelLoadResult::FAIL)
            {
                fprintf(stderr, "%s: failed to load model from '%s'\n", __func__, kcpp_params->model.c_str());
                return res;
            }
            else if(res==ModelLoadResult::RETRY_LOAD)
            {
                printf("\nIncorrect Tensor Size Detected! Retrying GPT-NeoX model loading...");
                return res;
            }

            n_vocab = neox_ctx_v2.hparams.n_vocab;

            // determine the required inference memory per token:
            gpt_neox_v2_eval(neox_ctx_v2, kcpp_params->n_threads, 0, { 0, 1, 2, 3 }, logits, mem_per_token);

            if(logits.size()>0 && file_format==FileFormat::NEOX_2 && !IsNanCheck(logits[0]))
            {
                //run the black magic eval to determine if it's redpajama. VERY UGLY HACK!
                std::vector<int> test_embd = ::gpt_tokenize(vocab, "1 2 3 4 5 6 7");
                auto orig_par_res = neox_ctx_v2.hparams.par_res;
                neox_ctx_v2.hparams.par_res = 0; //test with residual false
                gpt_neox_v2_eval(neox_ctx_v2, kcpp_params->n_threads, 0, test_embd, logits, mem_per_token);
                neox_ctx_v2.hparams.par_res = orig_par_res;
                int topid = std::max_element(logits.begin(),logits.end())-logits.begin();
                std::string predicted = vocab.id_to_token[topid].c_str();
                auto findresult = predicted.find("8");
                if(findresult != std::string::npos && findresult<2)
                {
                    printf("\n---\nOld RedPajama NeoX Detected! Switching to new format! (use_parallel_residual=False)\n");
                    ggml_v2_free(neox_ctx_v2.ctx);
                    return ModelLoadResult::RETRY_LOAD;
                }
            }

            return ModelLoadResult::SUCCESS;
        }

    }
    else if(file_format==FileFormat::MPT_1)
    {
        bool res = mpt_model_load(kcpp_params->model, mpt_ctx_v3, vocab, inputs.gpulayers);
        if(res==false)
        {
            fprintf(stderr, "%s: failed to load model from '%s'\n", __func__, kcpp_params->model.c_str());
            return ModelLoadResult::FAIL;
        }

        n_vocab = mpt_ctx_v3.hparams.n_vocab;

        // determine the required inference memory per token:
        mpt_eval(mpt_ctx_v3, kcpp_params->n_threads, 0, { 0, 1, 2, 3 }, logits, false, mem_per_token, v3_use_scratch);
        return ModelLoadResult::SUCCESS;
    }
    else
    {
        printf("\nUnknown Model, cannot load.\n");
        return ModelLoadResult::FAIL;
    }

}

bool gpttype_generate_abort()
{
    if(kcpp_params==nullptr)
    {
        printf("\nWarning: KCPP text generation not initialized!\n");
    }
    stopper_unused_tokens = remaining_tokens;
    remaining_tokens = 0;
    return true;
}

std::vector<int> gpttype_get_token_arr(const std::string & input, bool addbos)
{
    std::vector<int> toks;
    if(kcpp_params==nullptr)
    {
        printf("\nWarning: KCPP text generation not initialized!\n");
        return toks;
    }
    if(debugmode==1)
    {
        printf("\nFileFormat: %d, Tokenizing: %s",file_format ,input.c_str());
    }
    TokenizeString(input, toks, file_format,addbos);
    int tokcount = toks.size();
    if(debugmode==1)
    {
        printf("\nTokens Counted: %d\n",tokcount);
    }
    return toks;
}

const std::string & gpttype_get_pending_output()
{
    if(kcpp_params==nullptr)
    {
        printf("\nWarning: KCPP text generation not initialized!\n");
        return concat_output_reader_copy_poll;
    }
    concat_output_mtx.lock();
    concat_output_reader_copy_poll = concat_output;
    concat_output_mtx.unlock();
    return concat_output_reader_copy_poll;
}

bool VecContainsIntVal(const std::vector<int> & vec, const int val)
{
    for (const auto &matched : vec)
    {
        if (val == matched)
        {
            return true;
        }
    }
    return false;
}

int GetThreadsToUse(bool blasmode)
{
    if (blasmode)
    {
        if(!ggml_cpu_has_gpublas())
        {
            return 1;
        }
        else
        {
             return kcpp_params->n_threads_batch;
        }
    }
    return kcpp_params->n_threads;
}

generation_outputs gpttype_generate(const generation_inputs inputs)
{
    generation_outputs output;

    if(kcpp_params==nullptr)
    {
        printf("\nWarning: KCPP text generation not initialized!\n");
        output.text = nullptr;
        output.status = 0;
        output.stopreason = stop_reason::INVALID;
        generation_finished = true;
        return output;
    }

    if(debugmode==1 && file_format == FileFormat::GGUF_GENERIC)
    {
        llama_reset_timings(llama_ctx_v4);
    }

    concat_output_mtx.lock();
    concat_output = "";
    concat_output_reader_copy_poll = "";
    concat_output_reader_copy_res = "";
    concat_output_mtx.unlock();
    last_stop_reason = stop_reason::OUT_OF_TOKENS;
    stop_sequence.clear();
    special_stop_sequence.clear();
    for(int x=0;x<stop_token_max;++x)
    {
        std::string stopper = inputs.stop_sequence[x];
        if(stopper!="")
        {
            stop_sequence.push_back(stopper);

            //if it tokenizes to a single token, AND it's a single non-printable special token, use that
            std::vector<int> tmp;
            TokenizeString(stopper, tmp, file_format, false);

            if(tmp.size()==1) //tokenizes to exactly 1 special token
            {
                int specialid = tmp[0];
                std::string tokenizedstr = FileFormatTokenizeID(specialid, file_format);
                if(tokenizedstr=="") //must NOT have a text representation
                {
                    special_stop_sequence.push_back(specialid);
                }
            }
        }
    }

    //handle custom token bans
    banned_tokens.clear();
    for(int x=0;x<ban_token_max;++x)
    {
        std::string word = inputs.banned_tokens[x];
        if(word!="")
        {
            banned_tokens.push_back(word);
        }
    }
    banned_token_ids.clear();
    if(banned_tokens.size()>0)
    {
        if(debugmode==1)
        {
            printf("\nBanning %zu token sequences...",banned_tokens.size());
        }
        for(int v=0;v<n_vocab;++v)
        {
            std::string word = FileFormatTokenizeID(v,file_format, true);
            for(int i=0;i<banned_tokens.size();++i)
            {
                if (word.find(banned_tokens[i]) != std::string::npos)
                {
                    banned_token_ids.push_back(v);
                    break;
                }
            }
        }
        if(debugmode==1)
        {
            printf("\nBanned a total of %zu tokens.\n",banned_token_ids.size());
        }
    }

    logit_biases.clear();
    for(int x=0;x<logit_bias_max;++x)
    {
        int32_t t_id = inputs.logit_biases[x].token_id;
        float bias = inputs.logit_biases[x].bias;
        if(t_id >= 0 && t_id < n_vocab && bias!=0)
        {
           logit_biases.push_back(inputs.logit_biases[x]);
        }
    }

    std::string addedmemory = inputs.memory;

    //clear previous run llava embd memory, just-in-time free
    for(int i=0;i<llava_images.size();++i)
    {
        if(llava_images[i].b64data!="" && llava_images[i].clp_img_embd!=nullptr)
        {
            free(llava_images[i].clp_img_embd);
            llava_images[i].clp_img_embd = nullptr;
        }
    }
    llava_images.clear();
    std::string new_llava_composite = "";
    for(int x=0;x<images_max;++x)
    {
        std::string item = inputs.images[x];
        if(item!="")
        {
            llava_image lv;
            lv.b64data = item;
            llava_images.push_back(lv);
            new_llava_composite += item;
        }
    }
    if(llava_composite_image_signature!=new_llava_composite)
    {
        //images have changed. swap identifiers to force reprocessing
        current_llava_identifier = (current_llava_identifier==LLAVA_TOKEN_IDENTIFIER_A?LLAVA_TOKEN_IDENTIFIER_B:LLAVA_TOKEN_IDENTIFIER_A);
        llava_composite_image_signature = new_llava_composite;
        if(debugmode==1)
        {
            printf("\nLLAVA images changed, existing cache invalidated");
        }
    }

    kcpp_params->prompt = inputs.prompt;
    kcpp_params->seed = inputs.seed;
    kcpp_params->n_predict = inputs.max_length;
    kcpp_params->top_k = inputs.top_k;
    kcpp_params->top_p = inputs.top_p;
    kcpp_params->min_p = inputs.min_p;
    kcpp_params->typical_p = inputs.typical_p;
    kcpp_params->tfs_z = inputs.tfs;
    kcpp_params->temp = inputs.temperature;
    kcpp_params->repeat_last_n = inputs.rep_pen_range;
    kcpp_params->rep_pen_slope = inputs.rep_pen_slope;
    kcpp_params->repeat_penalty = inputs.rep_pen;
    kcpp_params->presence_penalty = inputs.presence_penalty;
    kcpp_params->mirostat = inputs.mirostat;
    kcpp_params->mirostat_eta = inputs.mirostat_eta;
    kcpp_params->mirostat_tau = inputs.mirostat_tau;
    kcpp_params->dry_multiplier = inputs.dry_multiplier;
    kcpp_params->dry_base = inputs.dry_base;
    kcpp_params->dry_allowed_length = inputs.dry_allowed_length;
    kcpp_params->dry_penalty_last_n = inputs.dry_penalty_last_n;
    kcpp_params->dynatemp_range = inputs.dynatemp_range;
    kcpp_params->dynatemp_exponent = inputs.dynatemp_exponent;
    kcpp_params->n_ctx = inputs.max_context_length;
    kcpp_params->smoothing_factor = inputs.smoothing_factor;

    // Parse dry sequence breakers / restart sequences
    kcpp_params->dry_sequence_breakers.clear();
    for(int x=0;x<dry_seq_break_max;++x) {
        std::string word = inputs.dry_sequence_breakers[x];
        if(word!="") {
            kcpp_params->dry_sequence_breakers.push_back(word);
        }
    }
    dry_sequence_breakers.clear();
    if(kcpp_params->dry_sequence_breakers.size()>0) {
        // Restrict the maximum length of sequences used as sequence breakers. There are
        // very few use cases for a long sequence breaker, and limiting the max length
        // prevents a potential denial of service attack in which long repetitive sequence
        // breakers could result in slow DRY sampling with a suitably crafted context.
        const int MAX_CHAR_LEN = 40;
        const int MAX_SEQ_LEN = 20;

        if(debugmode==1) {
            printf("\nProcessing %zu dry break strings...",kcpp_params->dry_sequence_breakers.size());
        }
        for (auto sequence_break: kcpp_params->dry_sequence_breakers) {
            if (sequence_break.size() > MAX_CHAR_LEN) {
                sequence_break.resize(MAX_CHAR_LEN);
            }
            GetOverlappingTokenSequences(sequence_break, dry_sequence_breakers, MAX_SEQ_LEN);
        }
        if(debugmode==1) {
            int trivial = 0, non_trivial = 0;
            for (const auto& seq: dry_sequence_breakers) {
                if (seq.second.empty()) {
                    ++trivial;
                } else {
                    ++non_trivial;
                }
            }
            printf("\nFound a total of %zu restart heads, %d trivial, %d non-trivial.\n", dry_sequence_breakers.size(), trivial, non_trivial);
        }
    }

    bool stream_sse = inputs.stream_sse;

    bool allow_regular_prints = (debugmode!=-1 && !inputs.quiet) || debugmode >= 1;

    generation_finished = false; // Set current generation status
    generated_tokens.clear(); // New Generation, new tokens

    std::string grammarstr = inputs.grammar;
    bool grammar_retain_state = inputs.grammar_retain_state;
    if(grammar_retain_state)
    {
        if(grammarstr=="" || current_grammar!=grammarstr) //if grammar is identical, retain state
        {
            load_grammar(grammarstr);
        }
    }
    else
    {
        load_grammar(grammarstr);
    }
    current_grammar = grammarstr;


    if (kcpp_params->repeat_last_n < 1)
    {
        kcpp_params->repeat_last_n = 1;
    }
    if (kcpp_params->rep_pen_slope > 1 || kcpp_params->rep_pen_slope<=0)
    {
        kcpp_params->rep_pen_slope = 1;
    }
    if (kcpp_params->top_k < 1)
    {
        kcpp_params->top_k = n_vocab; // all tokens in the vocabulary should be considered if top k is disabled
    }
    if (kcpp_params->seed <= 0 || kcpp_params->seed==0xFFFFFFFF)
    {
        kcpp_params->seed = (((uint32_t)time(NULL)) % 1000000u);
        if(debugmode==1)
        {
            printf("\nUsing Seed: %d",kcpp_params->seed);
        }
    }

    // tokenize the prompt
    std::vector<int> embd_inp;
    std::vector<int> embd_inp_mem; //for storing added memory
    std::vector<int> llava_mem; //for storing dummy tokens that will be consumed by llava
    std::vector<int> llava_sep; //to separate between different llava images

    int32_t nctx = kcpp_params->n_ctx;

    TokenizeString(kcpp_params->prompt, embd_inp, file_format);

    if(clp_ctx!=nullptr && clp_img_data!=nullptr)
    {
        TokenizeString("\n\n", llava_sep, file_format,false);
        int sepsize = llava_sep.size();

        for(int i=0;i<llava_images.size();++i)
        {
            std::string llava_image = llava_images[i].b64data;
            const std::vector<uint8_t> image_buffer = kcpp_base64_decode(llava_image);
            if (!clip_image_load_from_bytes(image_buffer.data(), image_buffer.size(), clp_img_data))
            {
                //failed to load image
                printf("\nError: Clip image %d failed to load!",i);
            }
            else
            {
                llava_images[i].clp_image_tokens = 0;
                if (!llava_image_embed_make_with_clip_img(clp_ctx, kcpp_params->n_threads, clp_img_data, &llava_images[i].clp_img_embd, &llava_images[i].clp_image_tokens)) {
                    printf("\nError: Clip image %d failed to create embd!",i);
                }
                if(debugmode==1)
                {
                    printf("\nLLAVA Clip Embed %i used Tokens: %d",i,llava_images[i].clp_image_tokens);
                }
                if(llava_images[i].clp_image_tokens>0 && llava_images[i].clp_image_tokens < nctx)
                {
                    int tokcnt = (i==0?(llava_images[i].clp_image_tokens):(llava_images[i].clp_image_tokens+sepsize));
                    for(int n=0;n<tokcnt;++n)
                    {
                        llava_mem.push_back(current_llava_identifier);
                    }
                }
                else
                {
                    printf("\nWarning: LLAVA Image excluded - Context size too low or not enough clip tokens!\n");
                }
            }
        }
    }

    if(addedmemory!="")
    {
        TokenizeString(addedmemory, embd_inp_mem, file_format);
    }

    //truncate to front of the prompt if its too long
    if (embd_inp.size() + kcpp_params->n_predict > nctx)
    {
        //get bos token
        std::vector<int> bos;
        TokenizeString("", bos, file_format);
        int offset = embd_inp.size() - nctx + kcpp_params->n_predict;
        embd_inp = std::vector<int>(embd_inp.begin() + offset, embd_inp.end());
        //replace bos into front if exists
        if(bos.size()>0 && embd_inp.size()>0)
        {
            embd_inp[0] = bos[0];
        }
    }

    if(llava_mem.size()>0) //stick the llava mem before the added mem
    {
        if(llava_mem.size() + kcpp_params->n_predict + 4 > nctx)
        {
            printf("\nWarning: Too many LLaVA tokens, max context exceeded! They will be ignored!\n");
        }
        else
        {
            std::vector<int> bos;
            TokenizeString("", bos, file_format);
            if(embd_inp_mem.size()>0) //remove existing bos if exists
            {
                if (bos.size()>0 && !embd_inp_mem.empty() && bos[0]==embd_inp_mem[0]) {
                    embd_inp_mem.erase(embd_inp_mem.begin());
                }
            }

            //append llava dummy tokens
            embd_inp_mem.insert(embd_inp_mem.begin(), llava_mem.begin(), llava_mem.end());
            if (bos.size() > 0 && embd_inp_mem.size() > 0)
            {
                embd_inp_mem.insert(embd_inp_mem.begin(), bos[0]);  //insert bos at front
            }

             //shorten memory if needed
            if (embd_inp_mem.size() + kcpp_params->n_predict + 4 > nctx)
            {
                int limit = nctx - (kcpp_params->n_predict + 4);
                if (embd_inp_mem.size() > limit) {
                    embd_inp_mem.resize(limit);
                }
            }
        }
    }

    //added special memory, overwrite if needed
    if(embd_inp_mem.size()>0)
    {
        //remove bos token from prompt, it'll be taken from memory
        std::vector<int> bos;
        TokenizeString("", bos, file_format);
        if (bos.size()>0 && !embd_inp.empty() && bos[0]==embd_inp[0]) {
            embd_inp.erase(embd_inp.begin());
        }

        //shorten memory if needed
        if (embd_inp_mem.size() + kcpp_params->n_predict + 4 > nctx)
        {
            int offset = embd_inp_mem.size() - nctx + kcpp_params->n_predict + 4;
            embd_inp_mem = std::vector<int>(embd_inp_mem.begin() + offset, embd_inp_mem.end());
            //replace bos into front if exists
            if(bos.size()>0 && embd_inp_mem.size()>0)
            {
                embd_inp_mem[0] = bos[0];
            }
        }

        //shorten main prompt by trimming the front if needed
        int addmemtokens = embd_inp_mem.size();
        int totalsize = (addmemtokens + embd_inp.size() + kcpp_params->n_predict);
        if(totalsize > nctx)
        {
            int excess = totalsize - nctx;
            if (embd_inp.size() >= excess) {
                embd_inp.erase(embd_inp.begin(), embd_inp.begin() + excess);
            } else {
                embd_inp.clear();
            }
        }

        //stick memory to front of prompt
        embd_inp.insert(embd_inp.begin(), embd_inp_mem.begin(), embd_inp_mem.end());
    }

    //determine how much npast we have to rewind from the current state
    std::vector<gpt_vocab::id> embd;

    int last_n_size = kcpp_params->repeat_last_n;
    last_n_tokens.resize(last_n_size);

    std::fill(last_n_tokens.begin(), last_n_tokens.end(), 0);
    n_past = 0;

    if (debugmode==1)
    {
        std::string outstr = "";
        printf("\n\n[Debug: Dump Raw Input Tokens, format: %d]\n", file_format);
        outstr += get_tok_vec_str(embd_inp);
        printf("%s\n", RemoveBell(outstr).c_str());
    }

    bool is_mamba = (file_format == FileFormat::GGUF_GENERIC && file_format_meta.model_architecture==GGUFArch::ARCH_MAMBA);

    if (file_format == FileFormat::RWKV_1 || file_format==FileFormat::RWKV_2 || is_mamba)
    {
        ContextFastForward(current_context_tokens, embd_inp, n_past, last_n_tokens, nctx, smartcontext, false, true);
        if(is_mamba)
        {
            if(n_past==0)
            {
                llama_kv_cache_clear(llama_ctx_v4);
            }
            else if(embd_inp.size()==0)
            {
                embd_inp.push_back(current_context_tokens[current_context_tokens.size()-1]);
                n_past -= 1;
            }
        }
    }
    else
    {
        bool triggersc = useSmartContext;
        if(useContextShift && (file_format == FileFormat::GGUF_GENERIC))
        {
            PurgeMissingTokens(llama_ctx_v4, current_context_tokens, embd_inp, inputs.max_length, nctx);
            triggersc = false;
        }
        ContextFastForward(current_context_tokens, embd_inp, n_past, last_n_tokens, nctx, smartcontext, triggersc, false);
        if(file_format == FileFormat::GGUF_GENERIC)
        {
            llama_kv_cache_seq_rm(llama_ctx_v4, 0, n_past, -1);
        }
    }

    bool blasmode = (embd_inp.size() >= 32 && ggml_cpu_has_blas() && kcpp_params->n_batch>=32);

    current_context_tokens.resize(n_past);

    remaining_tokens = kcpp_params->n_predict;
    stopper_unused_tokens = 0;
    int input_consumed = 0;
    std::mt19937 rng(kcpp_params->seed);

    //prepare sampler order
    std::vector<samplers> sampler_order;
    if(inputs.sampler_len<=0) //list by value
    {
        sampler_order = {
            KCPP_SAMPLER_REP_PEN,
            KCPP_SAMPLER_TOP_K,
            KCPP_SAMPLER_TOP_A,
            KCPP_SAMPLER_TFS,
            KCPP_SAMPLER_TYP,
            KCPP_SAMPLER_TOP_P,
            KCPP_SAMPLER_TEMP
        };
    }
    else
    {
        for(int i=0;i<inputs.sampler_len;++i)
        {
            sampler_order.push_back(inputs.sampler_order[i]);
        }
    }

    bool startedsampling = false;
    bool v3_use_scratch = true; //for normal inference always use scratch

    timer_start();
    double time1 = 0, time2 = 0;

    if(file_format == FileFormat::RWKV_1 || file_format==FileFormat::RWKV_2)
    {
        if(n_past==0)
        {
            if(file_format == FileFormat::RWKV_1)
            {
                rwkv_ctx_v2->state_in = nullptr;
            }
            else
            {
                rwkv_ctx_v3->state_in = nullptr;
            }
        }
        else
        {
            if (file_format == FileFormat::RWKV_1)
            {
                rwkv_ctx_v2->state_in = rwkv_ctx_v2->state_out;
            }
            else
            {
                rwkv_ctx_v3->state_in = rwkv_ctx_v3->state_out;
            }

            //if it's empty, push in the final previous token
            if(embd_inp.size()==0 && current_context_tokens.size()>0)
            {
                embd_inp.push_back(current_context_tokens[current_context_tokens.size()-1]);
                current_context_tokens.pop_back();
            }
        }
    }

    if(n_vocab<=0)
    {
        printf("\nWarning! n_vocab is invalid, maybe bad format!");
    }

    if(allow_regular_prints)
    {
        printf("\n");
    }

    if (debugmode==1)
    {
        std::string outstr = "";
        printf("\n[Debug: Dump Forwarded Input Tokens, format: %d]\n", file_format);
        outstr += get_tok_vec_str(embd_inp);
        outstr += "\n\n[Debug: n_past="+std::to_string(n_past)+" Context Size = " + std::to_string(current_context_tokens.size()) + "]\n";
        outstr += get_tok_vec_str(current_context_tokens);
        printf("%s\n\n", RemoveBell(outstr).c_str());
    }

    while (remaining_tokens > 0)
    {
        gpt_vocab::id id = 0;
        // predict
        unsigned int embdsize = embd.size();
        //print progress
        if (!startedsampling && allow_regular_prints)
        {
            printf("\rProcessing Prompt%s (%d / %zu tokens)", (blasmode ? " [BLAS]" : ""), input_consumed, embd_inp.size());
        }
        fflush(stdout);

        if (embdsize > 0)
        {

            bool evalres = false;

            if (file_format == FileFormat::GGML || file_format == FileFormat::GGHF || file_format == FileFormat::GGJT || file_format == FileFormat::GGJT_2)
            {
                evalres = (llama_v2_eval(llama_ctx_v2, embd.data(), embdsize, n_past, GetThreadsToUse(blasmode))==0);
            }
            else if(file_format == FileFormat::GGJT_3)
            {
                evalres = (llama_v3_eval(llama_ctx_v3, embd.data(), embdsize, n_past, GetThreadsToUse(blasmode))==0);
            }
            else if(file_format == FileFormat::GGUF_GENERIC)
            {
                evalres = (llama_decode(llama_ctx_v4, llama_batch_get_one(embd.data(), embdsize, n_past, 0))==0);
            }
            else if(file_format==FileFormat::RWKV_1 || file_format==FileFormat::RWKV_2)
            {
                if (file_format == FileFormat::RWKV_1)
                {
                    evalres = rwkv_v2_eval(rwkv_ctx_v2, embd[0], rwkv_ctx_v2->state_in, rwkv_ctx_v2->state_out, rwkv_ctx_v2->logits_out);
                    memcpy(logits.data(), rwkv_ctx_v2->logits_out, sizeof(float) * rwkv_vocab.size());
                    rwkv_ctx_v2->state_in = rwkv_ctx_v2->state_out;
                }
                else
                {
                    if(embd.size()>1)
                    {
                        evalres = rwkv_eval_sequence(rwkv_ctx_v3, GetThreadsToUse(blasmode), (uint32_t*)embd.data(), embd.size(), rwkv_ctx_v3->state_in, rwkv_ctx_v3->state_out, rwkv_ctx_v3->logits_out);
                    }
                    else
                    {
                    bool ignoreLogits = (!startedsampling && ((int)embd_inp.size() > input_consumed + 2));
                    evalres = rwkv_eval(rwkv_ctx_v3, GetThreadsToUse(blasmode), embd[0], rwkv_ctx_v3->state_in, rwkv_ctx_v3->state_out, ignoreLogits?nullptr:rwkv_ctx_v3->logits_out);
                    }

                    memcpy(logits.data(), rwkv_ctx_v3->logits_out, sizeof(float) * rwkv_vocab.size());
                    rwkv_ctx_v3->state_in = rwkv_ctx_v3->state_out;
                }
            }
            else if(file_format==FileFormat::GPT2_1)
            {
                evalres = legacy_gpt2_eval(gpt2_ctx_v1, GetThreadsToUse(blasmode), n_past, embd, logits, mem_per_token, file_format);
            }
            else if(file_format==FileFormat::GPT2_2 || file_format==FileFormat::GPT2_3)
            {
                evalres = gpt2_v2_eval(gpt2_ctx_v2, GetThreadsToUse(blasmode), n_past, embd, logits, mem_per_token, file_format);
            }
            else if(file_format==FileFormat::GPT2_4)
            {
                evalres = gpt2_eval(gpt2_ctx_v3, GetThreadsToUse(blasmode), n_past, embd, logits, mem_per_token, v3_use_scratch);
            }
            else if(file_format==FileFormat::NEOX_1 || file_format == FileFormat::NEOX_2 || file_format == FileFormat::NEOX_3 || file_format==FileFormat::NEOX_4 || file_format==FileFormat::NEOX_5)
            {
                evalres = gpt_neox_v2_eval(neox_ctx_v2, GetThreadsToUse(blasmode), n_past, embd, logits, mem_per_token);
            }
            else if(file_format==FileFormat::NEOX_6|| file_format==FileFormat::NEOX_7)
            {
                evalres = gpt_neox_eval(neox_ctx_v3, GetThreadsToUse(blasmode), n_past, embd, logits, mem_per_token, v3_use_scratch);
            }
            else if(file_format==FileFormat::GPTJ_1 || file_format==FileFormat::GPTJ_2)
            {
                evalres = legacy_gptj_eval(gptj_ctx_v1, GetThreadsToUse(blasmode), n_past, embd, logits, mem_per_token, file_format);
            }
            else if(file_format==FileFormat::GPTJ_3 || file_format==FileFormat::GPTJ_4)
            {
                evalres = gptj_v2_eval(gptj_ctx_v2, GetThreadsToUse(blasmode), n_past, embd, logits, mem_per_token);
            }
            else if(file_format==FileFormat::GPTJ_5)
            {
                evalres = gptj_eval(gptj_ctx_v3, GetThreadsToUse(blasmode), n_past, embd, logits, mem_per_token, v3_use_scratch);
            }
            else if(file_format==FileFormat::MPT_1)
            {
                evalres = mpt_eval(mpt_ctx_v3, GetThreadsToUse(blasmode), n_past, embd, logits, false, mem_per_token, v3_use_scratch);
            }
            else
            {
                printf("\nCannot find eval function\n");
            }

            if (!evalres)
            {
                fprintf(stderr, "\nFailed to predict at %d! Check your context buffer sizes!\n",n_past);
                output.text = nullptr;
                output.status = 0;
                output.stopreason = stop_reason::INVALID;
                generation_finished = true;
                return output;
            }
        }

        n_past += embd.size();
        embd.clear();
        if ((int)embd_inp.size() <= input_consumed)
        {
            // out of user input, sample next token
            const float top_k = kcpp_params->top_k;
            const float top_p = kcpp_params->top_p;
            const float min_p = kcpp_params->min_p;
            const float temp = kcpp_params->temp;
            const float top_a = inputs.top_a;
            const float repeat_penalty = kcpp_params->repeat_penalty;
            const float presence_penalty = kcpp_params->presence_penalty;
            const float typical_p = kcpp_params->typical_p;
            const float tfs_z = kcpp_params->tfs_z;
            const float dynatemp_range = kcpp_params->dynatemp_range;
            const float dynatemp_exponent = kcpp_params->dynatemp_exponent;
            const float smoothing_factor = kcpp_params->smoothing_factor;

            if (!startedsampling)
            {
                startedsampling = true;
                time1 = timer_check();
                timer_start();
                if(allow_regular_prints)
                {
                    printf("\n");
                }
            }

            unsigned int eosID = GetEosID(file_format, n_vocab);
            unsigned int eotID = GetEotID(file_format);
            float * logitsPtr;
            float lowestLogit = 0;
            int btsize = banned_token_ids.size();
            if(file_format == FileFormat::GGML || file_format == FileFormat::GGHF || file_format == FileFormat::GGJT || file_format == FileFormat::GGJT_2 || file_format == FileFormat::GGJT_3 || file_format == FileFormat::GGUF_GENERIC)
            {
                if(file_format == FileFormat::GGUF_GENERIC)
                {
                    logitsPtr = llama_get_logits(llama_ctx_v4);
                }
                else if(file_format == FileFormat::GGJT_3)
                {
                    logitsPtr = llama_v3_get_logits(llama_ctx_v3);
                }
                else
                {
                    logitsPtr = llama_v2_get_logits(llama_ctx_v2);
                }
                lowestLogit = LowestLogit(logitsPtr,n_vocab);
            }
            else
            {
                logitsPtr = logits.data();
                lowestLogit = LowestLogit(logits);
            }

            if (!inputs.allow_eos_token && !inputs.bypass_eos_token)
            {
                // set the logit of the eos token to very low to avoid sampling it
                logitsPtr[eosID] = lowestLogit;
                if(eotID!=-1)
                {
                    logitsPtr[eotID] = lowestLogit;
                }
            }
            if(btsize>0)
            {
                for(int t=0;t<btsize;++t)
                {
                    logitsPtr[banned_token_ids[t]]=lowestLogit;
                }
            }

            id = SampleLogits(logitsPtr, nctx, n_vocab, last_n_size, repeat_penalty, kcpp_params->rep_pen_slope, presence_penalty,
            top_k, top_a, top_p, min_p, typical_p, tfs_z, temp, rng,
            kcpp_params->mirostat, kcpp_params->mirostat_tau, kcpp_params->mirostat_eta,
            kcpp_params->dry_multiplier, kcpp_params->dry_base,
            kcpp_params->dry_allowed_length, kcpp_params->dry_penalty_last_n,
            sampler_order, grammar, dynatemp_range, dynatemp_exponent, smoothing_factor);

            if (grammar != nullptr) {
                grammar_accept_token(file_format, n_vocab, grammar, id);
            }

            last_n_tokens.erase(last_n_tokens.begin());
            last_n_tokens.push_back(id);
            current_context_tokens.push_back(id);

            // add it to the context
            embd.push_back(id);

            // decrement remaining sampling budget
            --remaining_tokens;

            for (auto eid : embd)
            {
                std::string tokenizedstr = FileFormatTokenizeID(eid, file_format, inputs.render_special);
                if(!inputs.render_special && (eid==eosID || (eid==eotID && eid!=-1) || VecContainsIntVal(special_stop_sequence,id))) //extra filter to avoid unwanted special tokens
                {
                    tokenizedstr = ""; //prevent render
                }
                if(stream_sse)
                {
                    generated_tokens.push_back(tokenizedstr);
                }
                concat_output_mtx.lock();
                concat_output += tokenizedstr;
                concat_output_mtx.unlock();
            }

            if (startedsampling && allow_regular_prints)
            {
                printf("\rGenerating (%d / %d tokens)", (kcpp_params->n_predict - remaining_tokens), kcpp_params->n_predict);
            }
            if(debugmode==1 && top_picks.size()>0)
            {
                printf(" [");
                bool firstloop = true;
                for (auto & pick : top_picks)
                {
                    if (!firstloop)
                    {
                        printf(" ");
                    }
                    firstloop = false;
                    std::string tokenizedstr = FileFormatTokenizeID(pick.id, file_format, true);
                    ::utreplace(tokenizedstr, "\n", "\\n");
                    printf("(%s %.2f%%)", RemoveBell(tokenizedstr).c_str(), pick.p*100);
                }
                printf("]\n");
            }

            bool earlystopped = false;
            if(!inputs.bypass_eos_token && inputs.allow_eos_token && (id==eosID || (id==eotID && id!=-1)))
            {
                stopper_unused_tokens = remaining_tokens;
                if(allow_regular_prints)
                {
                    printf("\n(EOS token triggered! ID:%d)",id);
                }
                remaining_tokens = 0;
                last_stop_reason = stop_reason::EOS_TOKEN_HIT;
                earlystopped = true;
            }

            if(!earlystopped)
            {
                for (const auto &matched : special_stop_sequence)
                {
                    if(id==matched)
                    {
                        stopper_unused_tokens = remaining_tokens;
                        if(allow_regular_prints)
                        {
                            printf("\n(Special Stop Token Triggered! ID:%d)",matched);
                        }
                        remaining_tokens = 0;
                        last_stop_reason = stop_reason::EOS_TOKEN_HIT;
                        earlystopped = true;
                        break;
                    }
                }
            }

            if(!earlystopped)
            {
                for (const auto &matched : stop_sequence)
                {
                    if (concat_output.find(matched) != std::string::npos)
                    {
                        stopper_unused_tokens = remaining_tokens;
                        remaining_tokens = 0;
                        if(allow_regular_prints)
                        {
                            auto match_clean = matched;
                            replace_all(match_clean, "\n", "\\n");
                            printf("\n(Stop sequence triggered: %s)", match_clean.c_str());
                        }
                        last_stop_reason = stop_reason::CUSTOM_STOPPER;
                        earlystopped = true;
                        break;
                    }
                }
            }

            fflush(stdout);
        }
        else
        {
            // some user input remains from prompt or interaction, forward it to processing
            while ((int)embd_inp.size() > input_consumed)
            {
                int currtoken = embd_inp[input_consumed];
                if(currtoken==LLAVA_TOKEN_IDENTIFIER_A || currtoken==LLAVA_TOKEN_IDENTIFIER_B) //special llava token hit
                {
                    //if partial batch, dispatch existing first
                    if(embd.size()>0)
                    {
                        break;
                    }
                    else
                    {
                        //batch is empty, do image processing
                        int llavatokenscounted = 0;
                        int llavatokensevaled = 0;
                        int sepsize = llava_sep.size();
                        while(input_consumed < embd_inp.size() && (embd_inp[input_consumed]==LLAVA_TOKEN_IDENTIFIER_A || embd_inp[input_consumed]==LLAVA_TOKEN_IDENTIFIER_B))
                        {
                            last_n_tokens.erase(last_n_tokens.begin());
                            last_n_tokens.push_back(currtoken);
                            current_context_tokens.push_back(currtoken);
                            ++input_consumed;
                            ++llavatokenscounted;
                        }
                        for(int i=0;i<llava_images.size();++i)
                        {
                            if(i>0 && sepsize>0)
                            {
                                //add a separator between each image
                                auto evr = llama_decode(llama_ctx_v4, llama_batch_get_one(llava_sep.data(), sepsize, n_past, 0));
                                if(evr!=0)
                                {
                                    printf("\nError when appending llava separator: %d\n",evr);
                                }
                                else
                                {
                                    printf("\rProcessing LLaVa Separator (%d tokens)",sepsize);
                                }
                                n_past += sepsize;
                                llavatokensevaled += sepsize;
                            }

                            if(allow_regular_prints)
                            {
                                printf("\rProcessing LLaVa Embedding %d (%d tokens)",(i+1), llava_images[i].clp_image_tokens);
                            }
                            bool err = kcpp_eval_image(llama_ctx_v4,llava_images[i].clp_img_embd,llava_images[i].clp_image_tokens,kcpp_params->n_batch,&n_past);
                            llavatokensevaled += llava_images[i].clp_image_tokens;
                            if(!err)
                            {
                                llava_composite_image_signature = ""; //force invalidate
                                fprintf(stderr, "\nFailed to eval llava image at %d!\n",n_past);
                                output.text = nullptr;
                                output.status = 0;
                                output.stopreason = stop_reason::INVALID;
                                generation_finished = true;
                                return output;
                            }
                        }
                        if(llavatokenscounted!=llavatokensevaled)
                        {
                            llava_composite_image_signature = ""; //force invalidate
                            fprintf(stderr, "\nLLAVA image tokens mismatch at %d! (%d vs %d tokens)\n",n_past,llavatokenscounted,llavatokensevaled);
                            output.text = nullptr;
                            output.status = 0;
                            output.stopreason = stop_reason::INVALID;
                            generation_finished = true;
                            return output;
                        }
                    }
                }
                else
                {
                    embd.push_back(currtoken);
                    last_n_tokens.erase(last_n_tokens.begin());
                    last_n_tokens.push_back(currtoken);
                    current_context_tokens.push_back(currtoken);
                    ++input_consumed;
                    if ((int)embd.size() >= kcpp_params->n_batch)
                    {
                        break;
                    }
                }

            }
        }
    }

    if(debugmode==1 && file_format == FileFormat::GGUF_GENERIC)
    {
        llama_print_timings(llama_ctx_v4);
    }

    time2 = timer_check();
    float pt1 = (time1*1000.0/(embd_inp.size()==0?1:embd_inp.size()));
    float ts1 = (1000.0/pt1);
    int realnpredict = kcpp_params->n_predict-stopper_unused_tokens;
    float pt2 = (time2*1000.0/(realnpredict==0?1:realnpredict));
    float ts2 = (1000.0/pt2);
    float tokens_per_second = (realnpredict == 0 ? 0 : realnpredict / (time1 + time2));
    printf("\nCtxLimit: %d/%d, Process:%.2fs (%.1fms/T = %.2fT/s), Generate:%.2fs (%.1fms/T = %.2fT/s), Total:%.2fs (%.2fT/s)",(int)current_context_tokens.size(),(int)nctx, time1, pt1, ts1, time2, pt2, ts2, (time1 + time2), tokens_per_second);
    fflush(stdout);
    output.status = 1;
    output.stopreason = last_stop_reason;
    generation_finished = true;
    last_eval_time = pt2;
    last_process_time = pt1;
    last_token_count = realnpredict;
    last_seed = kcpp_params->seed;
    total_gens += 1;
    concat_output_mtx.lock();
    concat_output_reader_copy_res = concat_output;
    concat_output_mtx.unlock();
    output.text = concat_output_reader_copy_res.c_str();
    return output;
}
