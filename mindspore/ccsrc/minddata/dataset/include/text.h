/**
 * Copyright 2020 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MINDSPORE_CCSRC_MINDDATA_DATASET_INCLUDE_TEXT_H_
#define MINDSPORE_CCSRC_MINDDATA_DATASET_INCLUDE_TEXT_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "mindspore/ccsrc/minddata/dataset/core/data_type.h"
#include "minddata/dataset/core/constants.h"
#include "minddata/dataset/include/transforms.h"
#include "minddata/dataset/util/status.h"

#include "minddata/dataset/text/kernels/sentence_piece_tokenizer_op.h"
#include "minddata/dataset/text/sentence_piece_vocab.h"
#include "minddata/dataset/text/vocab.h"

namespace mindspore {
namespace dataset {

// Transform operations for text
namespace text {

// Char arrays storing name of corresponding classes (in alphabetical order)
constexpr char kCaseFoldOperation[] = "CaseFold";
constexpr char kJiebaTokenizerOperation[] = "JiebaTokenizer";
constexpr char kLookupOperation[] = "Lookup";
constexpr char kNgramOperation[] = "Ngram";
constexpr char kNormalizeUTF8Operation[] = "NormalizeUTF8";
constexpr char kRegexReplaceOperation[] = "RegexReplace";
constexpr char kRegexTokenizerOperation[] = "RegexTokenizer";
constexpr char kSentencepieceTokenizerOperation[] = "SentencepieceTokenizer";
constexpr char kSlidingWindowOperation[] = "SlidingWindow";
constexpr char kUnicodeCharTokenizerOperation[] = "UnicodeCharTokenizer";
constexpr char kUnicodeScriptTokenizerOperation[] = "UnicodeScriptTokenizer";
constexpr char kWhitespaceTokenizerOperation[] = "WhitespaceTokenizer";

// Text Op classes (in alphabetical order)
#ifndef _WIN32
class CaseFoldOperation;
#endif
class JiebaTokenizerOperation;
class LookupOperation;
class NgramOperation;
#ifndef _WIN32
class NormalizeUTF8Operation;
class RegexReplaceOperation;
class RegexTokenizerOperation;
#endif
class SentencePieceTokenizerOperation;
class SlidingWindowOperation;
class UnicodeCharTokenizerOperation;
#ifndef _WIN32
class UnicodeScriptTokenizerOperation;
class WhitespaceTokenizerOperation;
#endif

#ifndef _WIN32
/// \brief Apply case fold operation on UTF-8 string tensor.
/// \return Shared pointer to the current TensorOperation.
std::shared_ptr<CaseFoldOperation> CaseFold();
#endif

/// \brief Tokenize Chinese string into words based on dictionary.
/// \param[in] hmm_path Dictionary file is used by HMMSegment algorithm. The dictionary can be obtained on the
///   official website of cppjieba.
/// \param[in] mp_path Dictionary file is used by MPSegment algorithm. The dictionary can be obtained on the
///   official website of cppjieba.
/// \param[in] mode Valid values can be any of [JiebaMode.MP, JiebaMode.HMM, JiebaMode.MIX](default=JiebaMode.MIX).
///   - JiebaMode.MP, tokenize with MPSegment algorithm.
///   - JiebaMode.HMM, tokenize with Hiddel Markov Model Segment algorithm.
///   - JiebaMode.MIX, tokenize with a mix of MPSegment and HMMSegment algorithm.
/// \param[in] with_offsets If or not output offsets of tokens (default=false).
/// \return Shared pointer to the current TensorOperation.
std::shared_ptr<JiebaTokenizerOperation> JiebaTokenizer(const std::string &hmm_path, const std::string &mp_path,
                                                        const JiebaMode &mode = JiebaMode::kMix,
                                                        bool with_offsets = false);

/// \brief Lookup operator that looks up a word to an id.
/// \param[in] vocab a Vocab object.
/// \param[in] unknown_token word to use for lookup if the word being looked up is out of Vocabulary (oov).
///   If unknown_token is oov, runtime error will be thrown.
/// \param[in] DataType type of the tensor after lookup, typically int32.
/// \return Shared pointer to the current TensorOperation.
std::shared_ptr<LookupOperation> Lookup(const std::shared_ptr<Vocab> &vocab, const std::string &unknown_token,
                                        const mindspore::dataset::DataType &data_type = DataType("int32"));

/// \brief TensorOp to generate n-gram from a 1-D string Tensor.
/// \param[in] ngrams ngrams is a vector of positive integers. For example, if ngrams={4, 3}, then the result
///   would be a 4-gram followed by a 3-gram in the same tensor. If the number of words is not enough to make up
///   for a n-gram, an empty string will be returned.
/// \param[in] left_pad {"pad_token", pad_width}. Padding performed on left side of the sequence. pad_width will
///   be capped at n-1. left_pad=("_",2) would pad left side of the sequence with "__" (default={"", 0}}).
/// \param[in] right_pad {"pad_token", pad_width}. Padding performed on right side of the sequence.pad_width will
///   be capped at n-1. right_pad=("-":2) would pad right side of the sequence with "--" (default={"", 0}}).
/// \param[in] separator Symbol used to join strings together (default=" ").
/// \return Shared pointer to the current TensorOperation.
std::shared_ptr<NgramOperation> Ngram(const std::vector<int32_t> &ngrams,
                                      const std::pair<std::string, int32_t> &left_pad = {"", 0},
                                      const std::pair<std::string, int32_t> &right_pad = {"", 0},
                                      const std::string &separator = " ");

#ifndef _WIN32
/// \brief Apply normalize operation on UTF-8 string tensor.
/// \param[in] normalize_form Valid values can be any of [NormalizeForm::kNone,NormalizeForm::kNfc,
///   NormalizeForm::kNfkc,
///   NormalizeForm::kNfd, NormalizeForm::kNfkd](default=NormalizeForm::kNfkc).
///   See http://unicode.org/reports/tr15/ for details.
///   - NormalizeForm.NONE, do nothing for input string tensor.
///   - NormalizeForm.NFC, normalize with Normalization Form C.
///   - NormalizeForm.NFKC, normalize with Normalization Form KC.
///   - NormalizeForm.NFD, normalize with Normalization Form D.
///   - NormalizeForm.NFKD, normalize with Normalization Form KD.
/// \return Shared pointer to the current TensorOperation.
std::shared_ptr<NormalizeUTF8Operation> NormalizeUTF8(NormalizeForm normalize_form = NormalizeForm::kNfkc);

/// \brief Replace UTF-8 string tensor with 'replace' according to regular expression 'pattern'.
/// \param[in] pattern The regex expression patterns.
/// \param[in] replace The string to replace matched element.
/// \param[in] replace_all Confirm whether to replace all. If false, only replace first matched element;
///   if true, replace all matched elements (default=true).
/// \return Shared pointer to the current TensorOperation.
std::shared_ptr<RegexReplaceOperation> RegexReplace(std::string pattern, std::string replace, bool replace_all = true);

/// \brief Tokenize a scalar tensor of UTF-8 string by regex expression pattern.
/// \param[in] delim_pattern The pattern of regex delimiters.
/// \param[in] keep_delim_pattern The string matched by 'delim_pattern' can be kept as a token if it can be
///   matched by 'keep_delim_pattern'. The default value is an empty string ("")
///   which means that delimiters will not be kept as an output token (default="").
/// \param[in] with_offsets If or not output offsets of tokens (default=false).
/// \return Shared pointer to the current TensorOperation.
std::shared_ptr<RegexTokenizerOperation> RegexTokenizer(std::string delim_pattern, std::string keep_delim_pattern = "",
                                                        bool with_offsets = false);
#endif

/// \brief Tokenize scalar token or 1-D tokens to tokens by sentencepiece.
/// \param[in] vocab a SentencePieceVocab object.
/// \param[in] out_type The type of output.
/// \return Shared pointer to the current TensorOperation.
std::shared_ptr<SentencePieceTokenizerOperation> SentencePieceTokenizer(
  const std::shared_ptr<SentencePieceVocab> &vocab, mindspore::dataset::SPieceTokenizerOutType out_type);

/// \brief Tokenize scalar token or 1-D tokens to tokens by sentencepiece.
/// \param[in] vocab_path vocab model file path.
/// \param[in] out_type The type of output.
/// \return Shared pointer to the current TensorOperation.
std::shared_ptr<SentencePieceTokenizerOperation> SentencePieceTokenizer(
  const std::string &vocab_path, mindspore::dataset::SPieceTokenizerOutType out_type);

/// \brief TensorOp to construct a tensor from data (only 1-D for now), where each element in the dimension
///   axis is a slice of data starting at the corresponding position, with a specified width.
/// \param[in] width The width of the window. It must be an integer and greater than zero.
/// \param[in] axis The axis along which the sliding window is computed (default=0), axis support 0 or -1 only
///   for now.
/// \return Shared pointer to the current TensorOperation.
std::shared_ptr<SlidingWindowOperation> SlidingWindow(const int32_t width, const int32_t axis = 0);

/// \brief Tokenize a scalar tensor of UTF-8 string to Unicode characters.
/// \param[in] with_offsets If or not output offsets of tokens (default=false).
/// \return Shared pointer to the current TensorOperation.
std::shared_ptr<UnicodeCharTokenizerOperation> UnicodeCharTokenizer(bool with_offsets = false);

#ifndef _WIN32
/// \brief Tokenize a scalar tensor of UTF-8 string on Unicode script boundaries.
/// \param[in] keep_whitespace If or not emit whitespace tokens (default=false).
/// \param[in] with_offsets If or not output offsets of tokens (default=false).
/// \return Shared pointer to the current TensorOperation.
std::shared_ptr<UnicodeScriptTokenizerOperation> UnicodeScriptTokenizer(bool keep_whitespace = false,
                                                                        bool with_offsets = false);

/// \brief Tokenize a scalar tensor of UTF-8 string on ICU4C defined whitespaces.
/// \param[in] with_offsets If or not output offsets of tokens (default=false).
/// \return Shared pointer to the current TensorOperation.
std::shared_ptr<WhitespaceTokenizerOperation> WhitespaceTokenizer(bool with_offsets = false);
#endif

/* ####################################### Derived TensorOperation classes ################################# */

#ifndef _WIN32
class CaseFoldOperation : public TensorOperation {
 public:
  CaseFoldOperation() = default;

  ~CaseFoldOperation() = default;

  std::shared_ptr<TensorOp> Build() override;

  Status ValidateParams() override;

  std::string Name() const override { return kCaseFoldOperation; }
};
#endif

class JiebaTokenizerOperation : public TensorOperation {
 public:
  explicit JiebaTokenizerOperation(const std::string &hmm_path, const std::string &mp_path, const JiebaMode &mode,
                                   bool with_offsets);

  ~JiebaTokenizerOperation() = default;

  std::shared_ptr<TensorOp> Build() override;

  Status ValidateParams() override;

  std::string Name() const override { return kJiebaTokenizerOperation; }

 private:
  std::string hmm_path_;
  std::string mp_path_;
  JiebaMode mode_;
  bool with_offsets_;
};

class LookupOperation : public TensorOperation {
 public:
  explicit LookupOperation(const std::shared_ptr<Vocab> &vocab, const std::string &unknown_token,
                           const DataType &data_type);

  ~LookupOperation() = default;

  std::shared_ptr<TensorOp> Build() override;

  Status ValidateParams() override;

  std::string Name() const override { return kLookupOperation; }

 private:
  std::shared_ptr<Vocab> vocab_;
  std::string unknown_token_;
  int32_t default_id_;
  DataType data_type_;
};

class NgramOperation : public TensorOperation {
 public:
  explicit NgramOperation(const std::vector<int32_t> &ngrams, const std::pair<std::string, int32_t> &left_pad,
                          const std::pair<std::string, int32_t> &right_pad, const std::string &separator);

  ~NgramOperation() = default;

  std::shared_ptr<TensorOp> Build() override;

  Status ValidateParams() override;

  std::string Name() const override { return kNgramOperation; }

 private:
  std::vector<int32_t> ngrams_;
  std::pair<std::string, int32_t> left_pad_;
  std::pair<std::string, int32_t> right_pad_;
  std::string separator_;
};

#ifndef _WIN32
class NormalizeUTF8Operation : public TensorOperation {
 public:
  explicit NormalizeUTF8Operation(NormalizeForm normalize_form);

  ~NormalizeUTF8Operation() = default;

  std::shared_ptr<TensorOp> Build() override;

  Status ValidateParams() override;

  std::string Name() const override { return kNormalizeUTF8Operation; }

 private:
  NormalizeForm normalize_form_;
};

class RegexReplaceOperation : public TensorOperation {
 public:
  RegexReplaceOperation(std::string pattern, std::string replace, bool replace_all);

  ~RegexReplaceOperation() = default;

  std::shared_ptr<TensorOp> Build() override;

  Status ValidateParams() override;

  std::string Name() const override { return kRegexReplaceOperation; }

 private:
  std::string pattern_;
  std::string replace_;
  bool replace_all_;
};

class RegexTokenizerOperation : public TensorOperation {
 public:
  explicit RegexTokenizerOperation(std::string delim_pattern, std::string keep_delim_pattern, bool with_offsets);

  ~RegexTokenizerOperation() = default;

  std::shared_ptr<TensorOp> Build() override;

  Status ValidateParams() override;

  std::string Name() const override { return kRegexTokenizerOperation; }

 private:
  std::string delim_pattern_;
  std::string keep_delim_pattern_;
  bool with_offsets_;
};
#endif

class SentencePieceTokenizerOperation : public TensorOperation {
 public:
  SentencePieceTokenizerOperation(const std::shared_ptr<SentencePieceVocab> &vocab, SPieceTokenizerOutType out_type);

  SentencePieceTokenizerOperation(const std::string &vocab_path, SPieceTokenizerOutType out_type);

  ~SentencePieceTokenizerOperation() = default;

  std::shared_ptr<TensorOp> Build() override;

  Status ValidateParams() override;

  std::string Name() const override { return kSentencepieceTokenizerOperation; }

 private:
  std::shared_ptr<SentencePieceVocab> vocab_;
  std::string vocab_path_;
  SPieceTokenizerLoadType load_type_;
  SPieceTokenizerOutType out_type_;
};

class SlidingWindowOperation : public TensorOperation {
 public:
  explicit SlidingWindowOperation(const int32_t width, const int32_t axis);

  ~SlidingWindowOperation() = default;

  std::shared_ptr<TensorOp> Build() override;

  Status ValidateParams() override;

  std::string Name() const override { return kSlidingWindowOperation; }

 private:
  int32_t width_;
  int32_t axis_;
};

class UnicodeCharTokenizerOperation : public TensorOperation {
 public:
  explicit UnicodeCharTokenizerOperation(bool with_offsets);

  ~UnicodeCharTokenizerOperation() = default;

  std::shared_ptr<TensorOp> Build() override;

  Status ValidateParams() override;

  std::string Name() const override { return kUnicodeCharTokenizerOperation; }

 private:
  bool with_offsets_;
};

#ifndef _WIN32
class UnicodeScriptTokenizerOperation : public TensorOperation {
 public:
  explicit UnicodeScriptTokenizerOperation(bool keep_whitespace, bool with_offsets);

  ~UnicodeScriptTokenizerOperation() = default;

  std::shared_ptr<TensorOp> Build() override;

  Status ValidateParams() override;

  std::string Name() const override { return kUnicodeScriptTokenizerOperation; }

 private:
  bool keep_whitespace_;
  bool with_offsets_;
};

class WhitespaceTokenizerOperation : public TensorOperation {
 public:
  explicit WhitespaceTokenizerOperation(bool with_offsets);

  ~WhitespaceTokenizerOperation() = default;

  std::shared_ptr<TensorOp> Build() override;

  Status ValidateParams() override;

  std::string Name() const override { return kWhitespaceTokenizerOperation; }

 private:
  bool with_offsets_;
};
#endif
}  // namespace text
}  // namespace dataset
}  // namespace mindspore
#endif  // MINDSPORE_CCSRC_MINDDATA_DATASET_INCLUDE_TEXT_H_
