#include "oneflow/core/common/util.h"
#include "oneflow/core/job/job_desc.h"
#include "oneflow/core/operator/operator.h"
#include "oneflow/core/eager/job_object.h"
#include "oneflow/core/eager/opkernel_object.h"
#include "oneflow/core/eager/blob_object.h"
#include "oneflow/core/vm/string_object.h"
#include "oneflow/core/vm/stream.msg.h"
#include "oneflow/core/vm/cuda_stream_type.h"
#include "oneflow/core/eager/opkernel_instruction.msg.h"
#include "oneflow/core/eager/opkernel_instruction_type.h"
#include "oneflow/core/vm/device_helper_stream_type.h"
#include "oneflow/core/vm/instruction.msg.h"
#include "oneflow/core/vm/instruction_type.h"
#include "oneflow/core/vm/object.h"

namespace oneflow {
namespace eager {

class NewOpKernelObjectInstructionType final : public vm::InstructionType {
 public:
  NewOpKernelObjectInstructionType() = default;
  ~NewOpKernelObjectInstructionType() override = default;

  using stream_type = vm::DeviceHelperStreamType;

  void Infer(vm::InstrCtx* instr_ctx) const override {
    FlatMsgView<NewOpKernelObjectInstrOperand> view;
    CHECK(view.Match(instr_ctx->instr_msg().operand()));
    const auto& job_object = instr_ctx->operand_type(view->job()).Get<JobObject>();
    for (int i = 0; i < view->op_size(); ++i) {
      CHECK_GT(view->op(i).logical_object_id(), 0);
      const OperatorConf& op_conf =
          job_object.OpConf4LogicalObjectId(view->op(i).logical_object_id());
      CHECK(op_conf.has_user_conf());
      instr_ctx->mut_operand_type(view->op(i))
          ->Mutable<OpKernelObject>(op_conf, job_object.job_desc(),
                                    job_object.parallel_desc().device_type());
    }
  }
  void Compute(vm::InstrCtx* instr_ctx) const override {
    // do nothing
  }
};
COMMAND(vm::RegisterInstructionType<NewOpKernelObjectInstructionType>("NewOpKernelObject"));
COMMAND(
    vm::RegisterLocalInstructionType<NewOpKernelObjectInstructionType>("NewLocalOpKernelObject"));

class DeleteOpKernelObjectInstructionType final : public vm::InstructionType {
 public:
  DeleteOpKernelObjectInstructionType() = default;
  ~DeleteOpKernelObjectInstructionType() override = default;

  using stream_type = vm::DeviceHelperStreamType;

  void Infer(vm::InstrCtx* instr_ctx) const override {
    FlatMsgView<DeleteOpKernelObjectInstrOperand> view;
    CHECK(view.Match(instr_ctx->instr_msg().operand()));
    for (int i = 0; i < view->op_size(); ++i) {
      auto* type_mirrored_object = instr_ctx->mut_operand_type(view->op(i));
      CHECK(type_mirrored_object->Has<OpKernelObject>());
      type_mirrored_object->reset_object();
    }
  }
  void Compute(vm::InstrCtx* instr_ctx) const override {
    // do nothing
  }
};
COMMAND(vm::RegisterInstructionType<DeleteOpKernelObjectInstructionType>("DeleteOpKernelObject"));
COMMAND(vm::RegisterLocalInstructionType<DeleteOpKernelObjectInstructionType>(
    "DeleteLocalOpKernelObject"));

namespace {

template<typename T, typename CallbackT>
void ForEachIbnAndLogicalObjectId(const vm::InstrCtx& instr_ctx, const T& args,
                                  const CallbackT& Callback) {
  CHECK_EQ(args.ibn_size(), args.input_index_size());
  CHECK_EQ(args.ibn_size(), args.input_blob_size());
  FOR_RANGE(int, i, 0, args.ibn_size()) {
    const std::string& bn_in_op =
        instr_ctx.operand_type(args.ibn(i)).template Get<vm::StringObject>().str();
    int64_t index = args.input_index(i);
    int64_t logical_object_id = args.input_blob(i).logical_object_id();
    Callback(bn_in_op, index, logical_object_id);
  }
}

template<typename T, typename CallbackT>
void ForEachIbnAndBlobObject(vm::InstrCtx* instr_ctx, const T& args, const CallbackT& Callback) {
  CHECK_EQ(args.ibn_size(), args.input_index_size());
  CHECK_EQ(args.ibn_size(), args.input_blob_size());
  FOR_RANGE(int, i, 0, args.ibn_size()) {
    const std::string& bn_in_op =
        instr_ctx->operand_type(args.ibn(i)).template Get<vm::StringObject>().str();
    int64_t index = args.input_index(i);
    const auto& blob_object =
        instr_ctx->operand_type(args.input_blob(i)).template Get<BlobObject>();
    Callback(GenRepeatedBn(bn_in_op, index), blob_object);
  }
}

template<typename T, typename CallbackT>
void ForEachObnAndLogicalObjectId(const vm::InstrCtx& instr_ctx, const T& args,
                                  const CallbackT& Callback) {
  CHECK_EQ(args.obn_size(), args.output_index_size());
  CHECK_EQ(args.obn_size(), args.output_blob_size());
  FOR_RANGE(int, i, 0, args.obn_size()) {
    const std::string& bn_in_op =
        instr_ctx.operand_type(args.obn(i)).template Get<vm::StringObject>().str();
    int64_t index = args.output_index(i);
    int64_t logical_object_id = args.output_blob(i).logical_object_id();
    Callback(bn_in_op, index, logical_object_id);
  }
  CHECK_EQ(args.mut2_obn_size(), args.mut2_output_index_size());
  CHECK_EQ(args.mut2_obn_size(), args.mut2_output_blob_size());
  FOR_RANGE(int, i, 0, args.mut2_obn_size()) {
    const std::string& bn_in_op =
        instr_ctx.operand_type(args.mut2_obn(i)).template Get<vm::StringObject>().str();
    int64_t index = args.mut2_output_index(i);
    int64_t logical_object_id = args.mut2_output_blob(i).logical_object_id();
    Callback(bn_in_op, index, logical_object_id);
  }
}

template<typename T, typename CallbackT>
void ForEachObnAndBlobObject(vm::InstrCtx* instr_ctx, const T& args, const CallbackT& Callback) {
  CHECK_EQ(args.obn_size(), args.output_index_size());
  CHECK_EQ(args.obn_size(), args.output_blob_size());
  FOR_RANGE(int, i, 0, args.obn_size()) {
    const std::string& bn_in_op =
        instr_ctx->operand_type(args.obn(i)).template Get<vm::StringObject>().str();
    int64_t index = args.output_index(i);
    auto* blob_object =
        instr_ctx->mut_operand_type(args.output_blob(i))->template Mut<BlobObject>();
    Callback(GenRepeatedBn(bn_in_op, index), blob_object);
  }
  CHECK_EQ(args.mut2_obn_size(), args.mut2_output_index_size());
  CHECK_EQ(args.mut2_obn_size(), args.mut2_output_blob_size());
  FOR_RANGE(int, i, 0, args.mut2_obn_size()) {
    const std::string& bn_in_op =
        instr_ctx->operand_type(args.mut2_obn(i)).template Get<vm::StringObject>().str();
    int64_t index = args.mut2_output_index(i);
    auto* blob_object =
        instr_ctx->mut_operand_type(args.mut2_output_blob(i))->template Mut<BlobObject>();
    Callback(GenRepeatedBn(bn_in_op, index), blob_object);
  }
}

template<typename T>
std::function<BlobDesc*(const std::string& bn_in_op)> MakeBlobDesc4BnInOp(vm::InstrCtx* instr_ctx,
                                                                          const T& args) {
  auto obn2blob_desc = std::make_shared<HashMap<std::string, BlobDesc*>>();
  {
    HashSet<const BlobDesc*> out_blob_descs;
    ForEachObnAndBlobObject(instr_ctx, args,
                            [&](const std::string& bn_in_op, BlobObject* blob_object) {
                              auto* blob_desc = blob_object->mut_blob_desc();
                              CHECK(out_blob_descs.insert(blob_desc).second);
                              CHECK(obn2blob_desc->emplace(bn_in_op, blob_desc).second);
                            });
  }
  auto ibn2blob_desc = std::make_shared<HashMap<std::string, const BlobDesc*>>();
  ForEachIbnAndBlobObject(
      instr_ctx, args, [&](const std::string& bn_in_op, const BlobObject& blob_object) {
        CHECK(ibn2blob_desc->emplace(bn_in_op, &blob_object.blob_desc()).second);
      });
  return [obn2blob_desc, ibn2blob_desc](const std::string& bn_in_op) -> BlobDesc* {
    auto output_iter = obn2blob_desc->find(bn_in_op);
    if (output_iter != obn2blob_desc->end()) { return output_iter->second; }
    auto input_iter = ibn2blob_desc->find(bn_in_op);
    if (input_iter != ibn2blob_desc->end()) { return const_cast<BlobDesc*>(input_iter->second); }
    return nullptr;
  };
}

template<typename T>
std::function<Blob*(const std::string& bn_in_op)> MakeBlob4BnInOp(vm::InstrCtx* instr_ctx,
                                                                  const T& args) {
  auto obn2blob = std::make_shared<HashMap<std::string, Blob*>>();
  ForEachObnAndBlobObject(instr_ctx, args,
                          [&](const std::string& bn_in_op, BlobObject* blob_object) {
                            CHECK(obn2blob->emplace(bn_in_op, blob_object->mut_blob()).second);
                          });
  auto ibn2blob = std::make_shared<HashMap<std::string, const Blob*>>();
  ForEachIbnAndBlobObject(instr_ctx, args,
                          [&](const std::string& bn_in_op, const BlobObject& blob_object) {
                            CHECK(ibn2blob->emplace(bn_in_op, &blob_object.blob()).second);
                          });
  return [obn2blob, ibn2blob](const std::string& bn_in_op) -> Blob* {
    auto output_iter = obn2blob->find(bn_in_op);
    if (output_iter != obn2blob->end()) { return output_iter->second; }
    auto input_iter = ibn2blob->find(bn_in_op);
    if (input_iter != ibn2blob->end()) { return const_cast<Blob*>(input_iter->second); }
    return nullptr;
  };
}

}  // namespace

template<typename T>
void CallOpKernelInstructionType::UpdateUserOpConfInputAndOutput(const vm::InstrCtx& instr_ctx,
                                                                 UserOpConf* user_op_conf,
                                                                 const T& args) const {
  user_op_conf->clear_input();
  ForEachIbnAndLogicalObjectId(
      instr_ctx, args,
      [user_op_conf](const std::string& ibn, int64_t i, int64_t logical_object_id) {
        auto* str_list = &(*user_op_conf->mutable_input())[ibn];
        CHECK_EQ(str_list->s_size(), i);
        str_list->add_s(std::string("0/") + std::to_string(logical_object_id));
      });
  user_op_conf->clear_output();
  ForEachObnAndLogicalObjectId(
      instr_ctx, args,
      [user_op_conf](const std::string& obn, int64_t i, int64_t logical_object_id) {
        auto* str_list = &(*user_op_conf->mutable_output())[obn];
        CHECK_EQ(str_list->s_size(), i);
        str_list->add_s(std::string("0/") + std::to_string(logical_object_id));
      });
}

void CallOpKernelInstructionType::Infer(vm::InstrCtx* instr_ctx) const {
  FlatMsgView<CallOpKernelInstrOperand> args(instr_ctx->instr_msg().operand());
  Infer(instr_ctx, args.Get());
}

template<typename T>
void CallOpKernelInstructionType::Infer(vm::InstrCtx* instr_ctx, const T& args) const {
  auto* opkernel_obj = instr_ctx->mut_operand_type(args.opkernel())->template Mut<OpKernelObject>();
  UpdateUserOpConfInputAndOutput(*instr_ctx, opkernel_obj->mut_user_op_conf(), args);
  opkernel_obj->ResetOpAndKernel(MakeBlobDesc4BnInOp(instr_ctx, args));
  ForEachObnAndBlobObject(instr_ctx, args, [](const std::string& _, BlobObject* blob_object) {
    blob_object->mutable_blob();
  });
}

void CallOpKernelInstructionType::Compute(vm::InstrCtx* instr_ctx) const {
  FlatMsgView<CallOpKernelInstrOperand> args(instr_ctx->instr_msg().operand());
  Compute(instr_ctx, args.Get());
}

template<typename T>
void CallOpKernelInstructionType::Compute(vm::InstrCtx* instr_ctx, const T& args) const {
  auto Blob4BnInOp = MakeBlob4BnInOp(instr_ctx, args);
  DeviceCtx* device_ctx = instr_ctx->mut_instr_chain()->stream().device_ctx().get();
  ForEachObnAndBlobObject(instr_ctx, args, [&](const std::string&, BlobObject* blob_object) {
    blob_object->TryAllocateBlobBodyMemory(device_ctx);
  });
  auto* opkernel_obj = instr_ctx->mut_operand_type(args.opkernel())->template Mut<OpKernelObject>();
  std::shared_ptr<user_op::OpKernelState> new_state;
  {
    EagerKernel* eager_kernel = opkernel_obj->mut_kernel();
    const auto& old_state = opkernel_obj->opkernel_state();
    new_state = eager_kernel->EagerModelForward(old_state, device_ctx, Blob4BnInOp);
  }
  opkernel_obj->reset_opkernel_state(new_state);
}

}  // namespace eager
}  // namespace oneflow