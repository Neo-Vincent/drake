#pragma once

#include "drake/common/drake_assert.h"
#include "drake/common/eigen_autodiff_types.h"
#include "drake/drakeSystemAnalysis_export.h"
#include "drake/systems/framework/context.h"
#include "drake/systems/framework/system.h"

namespace drake {
namespace systems {

/** Used to specify a particular choice of integration method.
Currently the default is 2nd order Runge Kutta (explicit trapezoid rule). **/
// TODO(sherm1) Replace with a more elaborate Integrator class and a much
// wider assortment of integrators.
enum class IntegratorType { UseDefault, ExplicitEuler, RungeKutta2 };

/** A forward dynamics solver for hybrid dynamic systems represented by
`System<T>` objects. Starting with an initial Context for a given System,
%Simulator advances time and produces a series of Context values that forms a
trajectory satisfying the system's dynamic equations to a specified accuracy.
Only the Context is modified by a %Simulator; the System is const.

A Drake System is a continuous/discrete/hybrid dynamic system where the
continuous part is a DAE, that is, it is expected to consist of a set of
differential equations and bilateral algebraic constraints. The set of active
constraints may change as a result of particular events, such as contact.

Given a current Context, we expect a System to provide us with
 - derivatives for the continuous differential equations that already satisfy
   the differentiated form of the constraints (typically, acceleration
   constraints),
 - a projection method for least-squares correction of violated higher-level
   constraints (position and velocity level),
 - a time-of-next-sample method that can be used to adjust the integrator
   stepsize in preparation for a discrete update,
 - a method that can update discrete variables when their sample time is
   reached,
 - witness (guard) functions for event isolation,
 - event handlers (reset functions) for making appropriate changes to state and
   mode variables when an event has been isolated.

The continuous parts of the trajectory are advanced using a numerical
integrator. Different integrators have different properties; if you know about
that you can choose the one that is most appropriate for your application.
Otherwise, a default is provided which is adequate for most systems. **/
// TODO(sherm1) When API stabilizes, should list the methods above in addition
// to describing them.
template <typename T>
class Simulator {
 public:
  /** Create a %Simulator that can advance a given System through time to
  produce a trajectory consisting of a sequence of Context values. The System
  must not have unresolved input ports if the values of those ports are
  necessary for computations performed during simulation (see class
  documentation).

  The Simulator holds an internal, non-owned reference to the System
  object so you must ensure that `system` has a longer lifetime than the
  %Simulator. It also owns a compatible Context internally that takes on each
  of the trajectory values. You may optionally provide a Context that will be
  used as the initial condition for the simulation; otherwise the %Simulator
  will obtain a default Context from `system`. **/
  explicit Simulator(const System<T>& system,
                     std::unique_ptr<ContextBase<T>> context = nullptr);

  /** Prepares the %Simulator for a simulation. This requires determining the
  integrator type, processing the options requested by the caller, and choosing
  an initial step size to attempt. If the initial Context does not satisfy the
  System's constraints, an attempt is made to modify the values of the
  continuous state variables to satisfy the constraints. This method will throw
  `std::logic_error` if the combination of options doesn't make sense, and
  `std::runtime_error` if it is unable to find a constraint-satisfying
  initial condition. **/
  // TODO(sherm1) Actually deal with constraints.
  void Initialize();

  /** Advance the System's trajectory until `final_time` is reached or some
  other termination condition occurs. The System's `Publish()` method is called
  at the start of each step. A variety of `std::runtime_error` conditions are
  possible here, as well as error conditions that may be thrown by the System
  when it is asked to perform computations. Be sure to enclose your simulation
  in a `try-catch` block and display the `what()` message.

  We recommend that you call `Initialize()` prior to making the first call to
  `StepTo()`. However, if you don't it will be called for you the first
  time you attempt a step, possibly resulting in unexpected error conditions.
  See documentation for `Initialize()` for the error conditions it might
  produce. **/
  // TODO(sherm1) Publish() should be called at publishing sample times.
  void StepTo(const T& final_time);

  /** Specify that a particular type of integrator be used for the continuous
  portions of the simulation. Otherwise a default integrator is chosen for
  you. **/
  void set_integrator_type(IntegratorType integrator) {
    req_integrator_ = integrator;
  }

  /** Report the type of integrator actually being used. **/
  IntegratorType get_integrator_type_in_use() const {
    return integrator_in_use_;
  }

  /** Request that the integrator attempt to achieve a particular accuracy for
  the continuous portions of the simulation. Otherwise a default accuracy is
  chosen for you. This is ignored for fixed-step integration since accuracy
  control requires variable step sizes.

  Integrators vary in the range of accuracy (loosest to tightest) that they can
  support. If you request accuracy outside the supported range for the chosen
  integrator it will be quietly adjusted to be in range. You can find out the
  accuracy setting actually being used using `get_accuracy_in_use()`.

  The precise meaning of *accuracy* is a complicated discussion, but translates
  roughly to the number of significant digits you want in the results. By
  convention it is supplied as `10^-digits`, meaning that an accuracy of 1e-3
  provides about three significant digits. For more information, see <pre>
    Sherman, et al. Procedia IUTAM 2:241-261 (2011), section 3.3.
    http://dx.doi.org/10.1016/j.piutam.2011.04.023
  </pre> **/
  // TODO(sherm1) Ignored at the moment.
  void set_accuracy(double accuracy) { req_accuracy_ = accuracy; }

  /** Report the accuracy setting actually being used. **/
  double get_accuracy_in_use() const { return accuracy_in_use_; }

  /** Request that the first attempted integration step have a particular size.
  Otherwise the integrator will estimate a suitable size for the initial step
  attempt. For variable-step integration this will be treated as a maximum size
  subject to accuracy requirements and event occurrences. You can find out what
  size *actually* worked with `get_actual_initial_step_size_taken()`. **/
  void request_initial_stepsize(double step_size) {
    req_initial_step_size_ = step_size;
  }

  /** Returns a const reference to the internally-maintained Context holding the
  most recent step in the trajectory. This is suitable for publishing or
  extracting information about this trajectory step. **/
  const ContextBase<T>& get_context() const { return *context_; }

  /** Returns a mutable pointer to the internally-maintained Context holding the
  most recent step in the trajectory. This is suitable for use in updates,
  sampling operations, event handlers, and constraint projection. You can
  also modify this prior to calling Initialize() to set initial conditions. **/
  ContextBase<T>* get_mutable_context() { return context_.get(); }

  /** Replace the internally-maintained Context with a different one. The
  current Context is deleted. This is useful for supplying non-default initial
  conditions. You should invoke Initialize() after replacing the Context. **/
  void reset_context(std::unique_ptr<Context<T>> context) {
    context_ = std::move(context);
    initialization_done_ = false;
  }

  /** Transfer ownership of this %Simulator's internal Context to the caller.
  The %Simulator will no longer contain a Context. **/
  std::unique_ptr<ContextBase<T>> release_context() {
    return std::move(context_);
    initialization_done_ = false;
  }

  /** @name                       Statistics
  These methods track relevant activity of the %Simulator since the last call
  to `Initialize()`. **/
  /**@{**/
  /** What what the actual size of the successful first step? **/
  T get_actual_initial_step_size_taken() const {
    return actual_initial_step_size_taken_;
  }

  /** What was the size of the smallest step taken since the last Initialize()
  call? **/
  T get_smallest_step_size_taken() const { return smallest_step_size_taken_; }

  /** What was the size of the largest step taken since the last Initialize()
  call? **/
  T get_largest_step_size_taken() const { return largest_step_size_taken_; }

  /** How many integration steps have been taken since the last Initialize()
  call? **/
  int64_t get_num_steps_taken() const { return num_steps_taken_; }

  /** How many discrete sample events have been processed since the last
  Initialize() call? **/
  int64_t get_num_discrete_samples() const { return num_discrete_samples_; }
  /**@}**/

 private:
  static constexpr double kDefaultAccuracy = 0.001;  // 1/10 of 1%.
  static constexpr IntegratorType kDefaultIntegrator =
      IntegratorType::RungeKutta2;
  static constexpr double kMaxStretch = 0.01;  // Allow 1% step size stretch.
  static constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

  const System<T>& system_;                  // Just a reference; not owned.
  std::unique_ptr<ContextBase<T>> context_;  // The trajectory Context.

  // TODO(sherm1) These are pre-allocated temporaries for use by Integrators;
  // move them to the Integrator classes when those exist. The actual number
  // needed varies for different integrators.
  std::unique_ptr<ContinuousState<T>> derivs0_;
  std::unique_ptr<ContinuousState<T>> derivs1_;

  // These are the caller's requests, if any.
  IntegratorType req_integrator_{IntegratorType::UseDefault};
  double req_accuracy_{0};           // means "unspecified, use default"
  double req_initial_step_size_{0};  // means "unspecified, use default"

  // This is set true by Initialize().
  bool initialization_done_{false};

  IntegratorType integrator_in_use_{IntegratorType::UseDefault};
  double accuracy_in_use_{kDefaultAccuracy};
  T initial_step_size_in_use_{1e-3};

  // This is set at the end of each step to guide the next one.
  T next_step_size_to_try_{1e-3};

  // Statistics.
  T actual_initial_step_size_taken_{kNaN};
  T smallest_step_size_taken_{kNaN};
  T largest_step_size_taken_{kNaN};
  int64_t num_steps_taken_{0};
  int64_t num_discrete_samples_{0};
};

// No need for user code to instantiate these; they are in the library.
extern template class Simulator<double>;
extern template class Simulator<AutoDiffXd>;

// TODO(sherm1) Move these implementations to an -inl.h file.

template <typename T>
Simulator<T>::Simulator(const System<T>& system,
                        std::unique_ptr<ContextBase<T>> context)
    : system_(system), context_(std::move(context)) {
  if (!context_) context_ = system_.CreateDefaultContext();

  // TODO(sherm1) Allocate temporaries in concrete integrators instead once
  // there are more of them.
  derivs0_ = system_.AllocateTimeDerivatives();
  derivs1_ = system_.AllocateTimeDerivatives();
}

template <typename T>
void Simulator<T>::Initialize() {
  // TODO(sherm1) Modify Context to satisfy constraints.

  if (req_integrator_ == IntegratorType::UseDefault)
    integrator_in_use_ = kDefaultIntegrator;
  else
    integrator_in_use_ = req_integrator_;

  if (req_accuracy_ > T(0)) accuracy_in_use_ = req_accuracy_;
  if (req_initial_step_size_ > T(0))
    initial_step_size_in_use_ = req_initial_step_size_;

  next_step_size_to_try_ = initial_step_size_in_use_;

  actual_initial_step_size_taken_ = T(kNaN);
  smallest_step_size_taken_ = T(kNaN);
  largest_step_size_taken_ = T(kNaN);
  num_steps_taken_ = 0;
  num_discrete_samples_ = 0;

  // TODO(sherm1) Anything to initialize in context?
  initialization_done_ = true;
}

template <typename T>
void Simulator<T>::StepTo(const T& final_time) {
  if (!initialization_done_) Initialize();

  // Find the continuous state xc within the Context, just once.
  StateVector<T>* xc =
      context_->get_mutable_state()->continuous_state->get_mutable_state();

  // TODO(sherm1) Invoke selected integrator.
  SampleActions sample_actions;
  bool sample_time_hit = false;
  while (context_->get_time() <= final_time) {
    // Starting a new step on the trajectory.
    const T step_start_time = context_->get_time();

    // First make any necessary discrete updates.
    if (sample_time_hit) {
      system_.Update(context_.get(), sample_actions);
      ++num_discrete_samples_;
    }

    // Now we can calculate start-of-step time derivatives.

    // TODO(sherm1) This should be calculating into the cache so that
    // Publish() doesn't have to recalculate if it wants to output
    // derivatives.
    system_.EvalTimeDerivatives(*context_, derivs0_.get());

    // The Context now contains the trajectory value at start-of-step.
    // Allow System a chance to produce some output.
    // TODO(sherm1) This should be called only at Publish sample times.
    system_.Publish(*context_);

    // That may have been the final trajectory entry.
    if (step_start_time == final_time) break;

    // Next, determine the end_time we are going to try to reach with the
    // next step. Start with the ideal step size.
    T step_end_time = step_start_time + next_step_size_to_try_;

    // We can be persuaded to take a slightly bigger step if necessary to
    // avoid a tiny sliver step before we have to do something discrete.
    const T step_stretch_time =
        step_end_time + kMaxStretch * next_step_size_to_try_;

    // How far can we go before we have to take a sampling break?
    const T sample_time =
        system_.CalcNextSampleTime(*context_, &sample_actions);
    DRAKE_ASSERT(sample_time >= step_start_time);

    // The step may be limited or stretched either by final time or sample
    // time, whichever comes sooner.
    sample_time_hit = false;
    if (sample_time <= final_time) {
      if (step_stretch_time >= sample_time) {
        step_end_time = sample_time;
        sample_time_hit = true;
      }
    } else {  // final_time < sample_time.
      if (step_stretch_time >= final_time) step_end_time = final_time;
    }

    if (step_end_time > step_start_time) {
      const T h = step_end_time - step_start_time;

      // First stage is an explicit Euler step:
      // xc(t+h) = xc(t) + h * xcdot(t, xc(t), xd(t+), u(t))
      const auto& xcdot0 = derivs0_->get_state();
      xc->PlusEqScaled(h, xcdot0);  // xc += h * xcdot0
      context_->set_time(step_end_time);

      // If we're using Explicit Euler, we're done.

      // For RK2, we need another stage. We want
      //    xc(t+h) = xc(t) + (h/2) * (xcdot0 + xcdot1)
      //            = [xc(t) + h * xcdot0] + (h/2) * (xcdot1 - xcdot0)
      if (get_integrator_type_in_use() == IntegratorType::RungeKutta2) {
        system_.EvalTimeDerivatives(*context_, derivs1_.get());
        const auto& xcdot1 = derivs1_->get_state();
        // TODO(sherm1) Use better operators when available.
        xc->PlusEqScaled(h / 2, xcdot1);
        xc->PlusEqScaled(-h / 2, xcdot0);
      }

      // TODO(sherm1) Constraint projection goes here.

      // TODO(sherm1) Accuracy control goes here.

      // We successfully took a step -- collect statistics.
      if (++num_steps_taken_ == 1) {  // The first step.
        actual_initial_step_size_taken_ = h;
        smallest_step_size_taken_ = h;
        largest_step_size_taken_ = h;
      } else {  // Not the first step.
        if (h < smallest_step_size_taken_) smallest_step_size_taken_ = h;
        if (h > largest_step_size_taken_) largest_step_size_taken_ = h;
      }
    }
  }
}

}  // namespace systems
}  // namespace drake
