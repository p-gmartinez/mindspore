# This is the Python adaptation and derivative work of Myia (https://github.com/mila-iqia/myia/).
#
# Copyright 2020-2021 Huawei Technologies Co., Ltd
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ============================================================================

"""Basic composite operations."""
from functools import partial
from types import FunctionType

from mindspore import context
from ..._c_expression import GradOperation_, HyperMap_, Map_, MultitypeFuncGraph_, Tail_, Shard_, \
    TupleAdd_, TupleSlice_, UnpackCall_, ZipOperation_, ListAppend_, TupleGetItemTensor_, ListInsert_, \
    ListSlice_, VmapOperation_
from ...common import dtype as mstype
from ...common.api import ms_function, _pynative_executor, _wrap_func
from ..primitive import Primitive
from ..operations import _grad_ops
from .. import operations as P
from .. import signature as sig

__all__ = [TupleAdd_, TupleSlice_, UnpackCall_, TupleGetItemTensor_, ListSlice_]


def add_flags(fn=None, **flags):
    """
    A decorator that adds a flag to the function.

    Note:
        Only supports bool value.

    Args:
        fn (Function): Function or cell to add flag. Default: None.
        flags (dict): Flags use kwargs. Default: None.

    Returns:
        Function, the function with added flags.

    Examples:
        >>> net = Net();
        >>> net = add_flags(net, predit=True)
        >>> print(hasattr(net, '_mindspore_flags'))
        True
    """
    def deco(fn):
        # need set the attr and access on c++
        if not hasattr(fn, "_mindspore_flags"):
            fn._mindspore_flags = {}

        fn._mindspore_flags.update({**flags})
        return fn
    ret = deco
    if fn is not None:
        ret = deco(fn)
    return ret


def core(fn=None, **flags):
    """
    A decorator that adds a flag to the function.

    By default, the function is marked as True, enabling to use this decorator to
    set flag to a graph.

    Args:
        fn (Function): Function to add flag. Default: None.
        flags (dict): The following flags can be set core, which indicates that this is a core function or
                      other flag. Default: None.

    Supported Platforms:
        ``Ascend`` ``GPU`` ``CPU``

    Examples:
        >>> net = Net()
        >>> net = core(net, predit=True)
        >>> print(hasattr(net, '_mindspore_flags'))
        True
    """
    # need set the attr and access on c++

    def deco(fn):
        fn._mindspore_flags = {
            'core': True,
            **flags,
        }
        return fn

    if fn is not None:
        ret = deco(fn)
    else:
        ret = deco
    return ret


class GradOperation(GradOperation_):
    """
    A higher-order function which is used to generate the gradient function for the input function.

    The gradient function generated by `GradOperation` higher-order function can be customized by
    construction arguments.

    Given an input function `net = Net()` that takes `x` and `y` as inputs, and has a parameter `z`,
    see `Net` in Examples.


    To generate a gradient function that returns gradients with respect to the first input
    (see `GradNetWrtX` in Examples).

    1. Construct a `GradOperation` higher-order function with default arguments:
       `grad_op = GradOperation()`.

    2. Call it with input function as argument to get the gradient function: `gradient_function = grad_op(net)`.

    3. Call the gradient function with input function's inputs to get the gradients with respect to the first input:
       `grad_op(net)(x, y)`.


    To generate a gradient function that returns gradients with respect to all inputs (see `GradNetWrtXY` in Examples).

    1. Construct a `GradOperation` higher-order function with `get_all=True` which
       indicates getting gradients with respect to all inputs, they are `x` and `y` in example function `Net()`:
       `grad_op = GradOperation(get_all=True)`.

    2. Call it with input function as argument to get the gradient function: `gradient_function = grad_op(net)`.

    3. Call the gradient function with input function's inputs to get the gradients with respect to all inputs:
       `gradient_function(x, y)`.

    To generate a gradient function that returns gradients with respect to given parameters
    (see `GradNetWithWrtParams` in Examples).

    1. Construct a `GradOperation` higher-order function with `get_by_list=True`:
       `grad_op = GradOperation(get_by_list=True)`.

    2. Construct a `ParameterTuple` that will be passed to the input function when constructing
       `GradOperation` higher-order function, it will be used as a parameter filter that determine
       which gradient to return: `params = ParameterTuple(net.trainable_params())`.

    3. Call it with input function and `params` as arguments to get the gradient function:
       `gradient_function = grad_op(net, params)`.

    4. Call the gradient function with input function's inputs to get the gradients with
       respect to given parameters: `gradient_function(x, y)`.

    To generate a gradient function that returns gradients with respect to all inputs and given parameters
    in the format of ((dx, dy), (dz))(see `GradNetWrtInputsAndParams` in Examples).

    1. Construct a `GradOperation` higher-order function with `get_all=True` and `get_by_list=True`:
       `grad_op = GradOperation(get_all=True, get_by_list=True)`.

    2. Construct a `ParameterTuple` that will be passed along input function when constructing
       `GradOperation` higher-order function: `params = ParameterTuple(net.trainable_params())`.

    3. Call it with input function and `params` as arguments to get the gradient function:
       `gradient_function = grad_op(net, params)`.

    4. Call the gradient function with input function's inputs
       to get the gradients with respect to all inputs and given parameters: `gradient_function(x, y)`.


    We can configure the sensitivity(gradient with respect to output) by setting `sens_param` as True and
    passing an extra sensitivity input to the gradient function, the sensitivity input should has the
    same shape and type with input function's output(see `GradNetWrtXYWithSensParam` in Examples).

    1. Construct a `GradOperation` higher-order function with `get_all=True` and `sens_param=True`:
       `grad_op = GradOperation(get_all=True, sens_param=True)`.

    2. Define `grad_wrt_output` as `sens_param` which works as the gradient with respect to output:
       `grad_wrt_output = Tensor(np.ones([2, 2]).astype(np.float32))`.

    3. Call it with input function as argument to get the gradient function:
       `gradient_function = grad_op(net)`.

    4. Call the gradient function with input function's inputs and `sens_param` to
       get the gradients with respect to all inputs:
       `gradient_function(x, y, grad_wrt_output)`.

    Args:
        get_all (bool): If True, get all the gradients with respect to inputs. Default: False.
        get_by_list (bool): If True, get all the gradients with respect to Parameter variables.
            If get_all and get_by_list are both False, get the gradient with respect to first input.
            If get_all and get_by_list are both True, get the gradients with respect to inputs and Parameter variables
            at the same time in the form of ((gradients with respect to inputs),
            (gradients with respect to parameters)). Default: False.
        sens_param (bool): Whether to append sensitivity (gradient with respect to output) as input.
            If sens_param is False, a 'ones_like(outputs)' sensitivity will be attached automatically.
            Default: False.
            If the sensor_param is True, a sensitivity (gradient with respect to output) needs to be transferred
            through the location parameter or key-value pair parameter. If the value is transferred through
            the key-value pair parameter, the key must be sens.

    Returns:
        The higher-order function which takes a function as argument and returns gradient function for it.

    Raises:
        TypeError: If `get_all`, `get_by_list` or `sens_param` is not a bool.

    Supported Platforms:
        ``Ascend`` ``GPU`` ``CPU``

    Examples:
        >>> from mindspore import ParameterTuple
        >>> from mindspore.ops.composite import GradOperation
        >>> from mindspore.ops import operations as P
        >>> class Net(nn.Cell):
        ...     def __init__(self):
        ...         super(Net, self).__init__()
        ...         self.matmul = P.MatMul()
        ...         self.z = Parameter(Tensor(np.array([1.0], np.float32)), name='z')
        ...     def construct(self, x, y):
        ...         x = x * self.z
        ...         out = self.matmul(x, y)
        ...         return out
        ...
        >>> class GradNetWrtX(nn.Cell):
        ...     def __init__(self, net):
        ...         super(GradNetWrtX, self).__init__()
        ...         self.net = net
        ...         self.grad_op = GradOperation()
        ...     def construct(self, x, y):
        ...         gradient_function = self.grad_op(self.net)
        ...         return gradient_function(x, y)
        ...
        >>> x = Tensor([[0.5, 0.6, 0.4], [1.2, 1.3, 1.1]], dtype=mstype.float32)
        >>> y = Tensor([[0.01, 0.3, 1.1], [0.1, 0.2, 1.3], [2.1, 1.2, 3.3]], dtype=mstype.float32)
        >>> output = GradNetWrtX(Net())(x, y)
        >>> print(output)
        [[1.4100001 1.5999999 6.6      ]
         [1.4100001 1.5999999 6.6      ]]
        >>>
        >>> class GradNetWrtXY(nn.Cell):
        ...     def __init__(self, net):
        ...         super(GradNetWrtXY, self).__init__()
        ...         self.net = net
        ...         self.grad_op = GradOperation(get_all=True)
        ...     def construct(self, x, y):
        ...         gradient_function = self.grad_op(self.net)
        ...         return gradient_function(x, y)
        >>>
        >>> x = Tensor([[0.8, 0.6, 0.2], [1.8, 1.3, 1.1]], dtype=mstype.float32)
        >>> y = Tensor([[0.1, 3.3, 1.1], [1.1, 0.2, 1.4], [1.1, 2.2, 0.3]], dtype=mstype.float32)
        >>> output = GradNetWrtXY(Net())(x, y)
        >>> print(output)
        (Tensor(shape=[2, 3], dtype=Float32, value=
        [[ 4.50000000e+00,  2.70000005e+00,  3.60000014e+00],
         [ 4.50000000e+00,  2.70000005e+00,  3.60000014e+00]]), Tensor(shape=[3, 3], dtype=Float32, value=
        [[ 2.59999990e+00,  2.59999990e+00,  2.59999990e+00],
         [ 1.89999998e+00,  1.89999998e+00,  1.89999998e+00],
         [ 1.30000007e+00,  1.30000007e+00,  1.30000007e+00]]))
        >>>
        >>> class GradNetWrtXYWithSensParam(nn.Cell):
        ...     def __init__(self, net):
        ...         super(GradNetWrtXYWithSensParam, self).__init__()
        ...         self.net = net
        ...         self.grad_op = GradOperation(get_all=True, sens_param=True)
        ...         self.grad_wrt_output = Tensor([[0.1, 0.6, 0.2], [0.8, 1.3, 1.1]], dtype=mstype.float32)
        ...     def construct(self, x, y):
        ...         gradient_function = self.grad_op(self.net)
        ...         return gradient_function(x, y, self.grad_wrt_output)
        >>>
        >>> x = Tensor([[0.8, 0.6, 0.2], [1.8, 1.3, 1.1]], dtype=mstype.float32)
        >>> y = Tensor([[0.11, 3.3, 1.1], [1.1, 0.2, 1.4], [1.1, 2.2, 0.3]], dtype=mstype.float32)
        >>> output = GradNetWrtXYWithSensParam(Net())(x, y)
        >>> print(output)
        (Tensor(shape=[2, 3], dtype=Float32, value=
        [[ 2.21099997e+00,  5.09999990e-01,  1.49000001e+00],
         [ 5.58800030e+00,  2.68000007e+00,  4.07000017e+00]]), Tensor(shape=[3, 3], dtype=Float32, value=
        [[ 1.51999998e+00,  2.81999993e+00,  2.14000010e+00],
         [ 1.09999990e+00,  2.04999995e+00,  1.54999995e+00],
         [ 9.00000036e-01,  1.54999995e+00,  1.25000000e+00]]))
        >>>
        >>> class GradNetWithWrtParams(nn.Cell):
        ...     def __init__(self, net):
        ...         super(GradNetWithWrtParams, self).__init__()
        ...         self.net = net
        ...         self.params = ParameterTuple(net.trainable_params())
        ...         self.grad_op = GradOperation(get_by_list=True)
        ...     def construct(self, x, y):
        ...         gradient_function = self.grad_op(self.net, self.params)
        ...         return gradient_function(x, y)
        >>>
        >>> x = Tensor([[0.8, 0.6, 0.2], [1.8, 1.3, 1.1]], dtype=mstype.float32)
        >>> y = Tensor([[0.11, 3.3, 1.1], [1.1, 0.2, 1.4], [1.1, 2.2, 0.3]], dtype=mstype.float32)
        >>> output = GradNetWithWrtParams(Net())(x, y)
        >>> print(output)
        (Tensor(shape=[1], dtype=Float32, value= [ 2.15359993e+01]),)
        >>>
        >>> class GradNetWrtInputsAndParams(nn.Cell):
        ...     def __init__(self, net):
        ...         super(GradNetWrtInputsAndParams, self).__init__()
        ...         self.net = net
        ...         self.params = ParameterTuple(net.trainable_params())
        ...         self.grad_op = GradOperation(get_all=True, get_by_list=True)
        ...     def construct(self, x, y):
        ...         gradient_function = self.grad_op(self.net, self.params)
        ...         return gradient_function(x, y)
        >>>
        >>> x = Tensor([[0.1, 0.6, 1.2], [0.5, 1.3, 0.1]], dtype=mstype.float32)
        >>> y = Tensor([[0.12, 2.3, 1.1], [1.3, 0.2, 2.4], [0.1, 2.2, 0.3]], dtype=mstype.float32)
        >>> output = GradNetWrtInputsAndParams(Net())(x, y)
        >>> print(output)
        ((Tensor(shape=[2, 3], dtype=Float32, value=
        [[ 3.51999998e+00,  3.90000010e+00,  2.59999990e+00],
         [ 3.51999998e+00,  3.90000010e+00,  2.59999990e+00]]), Tensor(shape=[3, 3], dtype=Float32, value=
        [[ 6.00000024e-01,  6.00000024e-01,  6.00000024e-01],
         [ 1.89999998e+00,  1.89999998e+00,  1.89999998e+00],
         [ 1.30000007e+00,  1.30000007e+00,  1.30000007e+00]])), (Tensor(shape=[1], dtype=Float32, value=
         [ 1.29020004e+01]),))
    """

    def __init__(self, get_all=False, get_by_list=False, sens_param=False):
        """Initialize GradOperation."""
        if not isinstance(get_all, bool):
            raise TypeError(f"For 'GradOperation', the 'get_all' should be bool, but got {type(get_all).__name__}")
        if not isinstance(get_by_list, bool):
            raise TypeError(f"For 'GradOperation', the 'get_by_list' should be bool, "
                            f"but got {type(get_by_list).__name__}")
        if not isinstance(sens_param, bool):
            raise TypeError(f"For 'GradOperation', the 'sens_param' should be bool, "
                            f"but got {type(sens_param).__name__}")
        self.get_all = get_all
        self.get_by_list = get_by_list
        self.sens_param = sens_param
        GradOperation_.__init__(self, 'grad', get_all, get_by_list, sens_param, False)
        self.grad_fn = None
        self.fn = None
        self.pynative_ = False

    def _pynative_forward_run(self, grad, args, kwargs, fn):
        """ Pynative forward run to build grad graph. """
        new_kwargs = kwargs
        if self.sens_param:
            if not 'sens' in kwargs.keys():
                args = args[:-1]
            else:
                new_kwargs = kwargs.copy()
                new_kwargs.pop('sens')
        if isinstance(fn, FunctionType):
            if not _pynative_executor.check_run(grad, fn, *args, **new_kwargs):
                _pynative_executor.set_grad_flag(True)
                _pynative_executor.new_graph(fn, *args, **new_kwargs)
                output = fn(*args, **new_kwargs)
                _pynative_executor.end_graph(fn, output, *args, **new_kwargs)
        else:
            # Check if fn have run already
            if not _pynative_executor.check_run(grad, fn, *args, **new_kwargs):
                fn.set_grad()
                fn(*args, **new_kwargs)
                fn.set_grad(False)

    def __call__(self, fn, weights=None):
        if self.grad_fn is not None and self.fn == fn:
            return self.grad_fn
        grad_ = GradOperation(self.get_all, self.get_by_list, self.sens_param)
        # If calling Grad in GRAPH_MODE or calling Grad in ms_function, do grad in GRAPH_MODE
        # If calling Grad in pure PYNATIVE_MODE do grad in PYNATIVE_MODE
        #   In pure PYNATIVE_MODE the out layer after_grad just used to set pynative flag for inner GradOperation.
        #   In PYNATIVE_MODE calling Grad from ms_function, use the out layer after_grad do grad in GRAPH_MODE.
        if context.get_context("mode") == context.GRAPH_MODE:
            if self.get_by_list:
                @ms_function
                def after_grad(*args):
                    return grad_(fn, weights)(*args)
            else:
                @ms_function
                def after_grad(*args):
                    return grad_(fn)(*args)
        elif self.pynative_:
            @_wrap_func
            def after_grad(*args, **kwargs):
                if _pynative_executor.check_graph(fn, *args, **kwargs):
                    print("Another grad step is running")
                self._pynative_forward_run(grad_, args, kwargs, fn)
                _pynative_executor.grad(grad_, fn, weights, (0,), *args, **kwargs)
                out = _pynative_executor(fn, *args, **kwargs)
                _pynative_executor.clear_grad(fn, *args, **kwargs)
                return out
        else:
            grad_.pynative_ = True
            # after_grad of this branch can't use @ms_function, just directly call grad_
            if self.get_by_list:
                def after_grad(*args, **kwargs):
                    return grad_(fn, weights)(*args, **kwargs)
            else:
                def after_grad(*args, **kwargs):
                    return grad_(fn)(*args, **kwargs)

        self.grad_fn = after_grad
        self.fn = fn
        return self.grad_fn


class _Grad(GradOperation_):
    """
    A higher-order function which is used to generate the gradient function by position for the input function.
    """

    def __init__(self, get_by_list=False, sens_param=False, get_by_position=False):
        """Initialize _Grad."""
        if not isinstance(get_by_position, bool):
            raise TypeError(f"For '_Grad', the 'get_by_position' should be bool, "
                            f"but got {type(get_by_position).__name__}")
        if not isinstance(get_by_list, bool):
            raise TypeError(f"For '_Grad', the 'get_by_list' should be bool, "
                            f"but got {type(get_by_list).__name__}")
        if not isinstance(sens_param, bool):
            raise TypeError(f"For '_Grad', the 'sens_param' should be bool, "
                            f"but got {type(sens_param).__name__}")
        self.get_by_position = get_by_position
        self.get_by_list = get_by_list
        self.sens_param = sens_param
        GradOperation_.__init__(self, 'grad', False, get_by_list, sens_param, get_by_position)
        self.grad_fn = None
        self.fn = None
        self.pynative_ = False
        self.grad_position = None

    def _pynative_forward_run(self, grad, args, kwargs, fn, grad_position):
        """ Pynative forward run to build grad graph. """
        new_kwargs = kwargs
        if self.sens_param:
            if not 'sens' in kwargs.keys():
                args = args[:-1]
            else:
                new_kwargs = kwargs.copy()
                new_kwargs.pop('sens')
        if isinstance(fn, FunctionType):
            if not _pynative_executor.check_run(grad, fn, *args, **new_kwargs):
                _pynative_executor.set_grad_flag(True)
                _pynative_executor.new_graph(fn, *args, **new_kwargs)
                output = fn(*args, **new_kwargs)
                _pynative_executor.end_graph(fn, output, *args, **new_kwargs)
        else:
            # Check if fn have run already
            if not _pynative_executor.check_run(grad, fn, *args, **new_kwargs):
                fn.set_grad()
                fn(*args, **new_kwargs)
                fn.set_grad(False)

    def __call__(self, fn, weights=None, grad_position=0):
        if self.grad_fn is not None and self.fn == fn and self.grad_position == grad_position:
            return self.grad_fn
        grad_ = _Grad(self.get_by_list, self.sens_param, self.get_by_position)
        # If calling Grad in GRAPH_MODE or calling Grad in ms_function, do grad in GRAPH_MODE
        # If calling Grad in pure PYNATIVE_MODE do grad in PYNATIVE_MODE
        #   In pure PYNATIVE_MODE the out layer after_grad just used to set pynative flag for inner GradOperation.
        #   In PYNATIVE_MODE calling Grad from ms_function, use the out layer after_grad do grad in GRAPH_MODE.
        if context.get_context("mode") == context.GRAPH_MODE:
            if self.get_by_position:
                @ms_function
                def after_grad(*args):
                    return grad_(fn, weights, grad_position)(*args)
            else:
                if self.get_by_list:
                    @ms_function
                    def after_grad(*args):
                        return grad_(fn, weights)(*args)
                else:
                    @ms_function
                    def after_grad(*args):
                        return grad_(fn)(*args)
        elif self.pynative_:
            _pynative_executor.set_grad_position(grad_, grad_position)
            @_wrap_func
            def after_grad(*args, **kwargs):
                if _pynative_executor.check_graph(fn, *args, **kwargs):
                    print("Another grad step is running")
                self._pynative_forward_run(grad_, args, kwargs, fn, grad_position)
                _pynative_executor.grad(grad_, fn, weights, grad_position, *args, **kwargs)
                out = _pynative_executor(fn, *args, **kwargs)
                _pynative_executor.clear_grad(fn, *args, **kwargs)
                return out
        else:
            grad_.pynative_ = True
            # after_grad of this branch can't use @ms_function, just directly call grad_
            if self.get_by_position:
                def after_grad(*args, **kwargs):
                    return grad_(fn, weights, grad_position)(*args, **kwargs)
            else:
                if self.get_by_list:
                    def after_grad(*args, **kwargs):
                        return grad_(fn, weights)(*args, **kwargs)
                else:
                    def after_grad(*args, **kwargs):
                        return grad_(fn)(*args, **kwargs)

        self.grad_fn = after_grad
        self.fn = fn
        self.grad_position = grad_position
        return self.grad_fn


class _Vmap(VmapOperation_):
    """
    A higher-order function which is used to generate the vectorizing map function.
    """

    def __init__(self):
        """Initialize _Vmap."""
        VmapOperation_.__init__(self, 'vmap')
        self.vmap_fn = None
        self.fn = None

    def __call__(self, fn, in_axes=0, out_axes=0):
        vmap_ = self

        @ms_function
        def after_vmap(*args):
            return vmap_(fn, in_axes, out_axes)(*args)

        self.vmap_fn = after_vmap
        self.fn = fn
        return self.vmap_fn


class MultitypeFuncGraph(MultitypeFuncGraph_):
    """
    Generates overloaded functions.

    MultitypeFuncGraph is a class used to generate overloaded functions, considering different types as inputs.
    Initialize an `MultitypeFuncGraph` object with name, and use `register` with input types as the decorator
    for the function to be registered. And the object can be called with different types of inputs,
    and work with `HyperMap` and `Map`.

    Args:
        name (str): Operator name.
        read_value (bool): If the registered function not need to set value on Parameter,
            and all inputs will pass by value, set `read_value` to True. Default: False.

    Raises:
        ValueError: If failed to find a matching function for the given arguments.

    Supported Platforms:
        ``Ascend`` ``GPU`` ``CPU``

    Examples:
        >>> # `add` is a metagraph object which will add two objects according to
        >>> # input type using ".register" decorator.
        >>> from mindspore import Tensor
        >>> from mindspore import ops
        >>> from mindspore import dtype as mstype
        >>> from mindspore.ops.composite import MultitypeFuncGraph
        >>>
        >>> tensor_add = ops.Add()
        >>> add = MultitypeFuncGraph('add')
        >>> @add.register("Number", "Number")
        ... def add_scala(x, y):
        ...     return x + y
        >>> @add.register("Tensor", "Tensor")
        ... def add_tensor(x, y):
        ...     return tensor_add(x, y)
        >>> output = add(1, 2)
        >>> print(output)
        3
        >>> output = add(Tensor([0.1, 0.6, 1.2], dtype=mstype.float32), Tensor([0.1, 0.6, 1.2], dtype=mstype.float32))
        >>> print(output)
        [0.2 1.2 2.4]
    """

    def __init__(self, name, read_value=False):
        """Initialize MultitypeFuncGraph."""
        MultitypeFuncGraph_.__init__(self, name)
        self.entries = list()
        if read_value:
            self.set_signatures((
                sig.make_sig('args', sig.sig_rw.RW_READ, sig.sig_kind.KIND_VAR_POSITIONAL),))

    def __call__(self, *args):
        if len(self.entries) == 1:
            output = self.entries[0][1](*args)
            return output
        types = tuple(map(mstype.get_py_obj_dtype, args))
        for sigs, fn in self.entries:
            if len(sigs) != len(types):
                continue
            if any(not mstype.issubclass_(type_, sig) for sig, type_ in zip(sigs, types)):
                continue
            output = fn(*args)
            return output
        raise ValueError(f"For 'MultitypeFuncGraph', cannot find fn match given args. Got (sigs, fn): {self.entries}, "
                         f"and (dtype, args): {types}.")

    def register(self, *type_names):
        """
        Register a function for the given type string.

        Args:
            type_names (Union[str, :class:`mindspore.dtype`]): Inputs type names or types list.

        Return:
            decorator, a decorator to register the function to run, when called under the
            types described in `type_names`.
        """
        def deco(fn):
            def convert_type(type_input):
                if isinstance(type_input, str):
                    return mstype.typing.str_to_type(type_input)
                if not isinstance(type_input, mstype.Type):
                    raise TypeError(f"For 'MultitypeFuncGraph', register only support str or {mstype.Type}, but got "
                                    f"'type_input': {type_input}.")
                return type_input

            types = tuple(map(convert_type, type_names))
            self.register_fn(type_names, fn)
            self.entries.append((types, fn))
            return fn
        return deco


class HyperMap(HyperMap_):
    """
    Hypermap will apply the set operation to input sequences.

    Apply the operations to every element of the sequence or nested sequence. Different
    from `Map`, the `HyperMap` supports to apply on nested structure.

    Args:
        ops (Union[MultitypeFuncGraph, None]): `ops` is the operation to apply. If `ops` is `None`,
            the operations should be put in the first input of the instance. Default is None.
        reverse (bool): The optimizer needs to be inverted in some scenarios to improve parallel performance,
          general users please ignore. `reverse` is the flag to decide if apply the operation reversely.
          Only supported in graph mode. Default is False.

    Inputs:
        - **args** (Tuple[sequence]) - If `ops` is not `None`, all the inputs should be sequences with the same length.
          And each row of the sequences will be the inputs of the operation.

          If `ops` is `None`, the first input is the operation, and the others are inputs.

    Note:
        Except for the operation input, the number of inputs should be equal to the number of inputs to `ops`.

    Outputs:
        Sequence or nested sequence, the sequence of output after applying the function.
        e.g. `operation(args[0][i], args[1][i])`.

    Raises:
        TypeError: If `ops` is neither MultitypeFuncGraph nor None.
        TypeError: If `args` is not a Tuple.

    Supported Platforms:
        ``Ascend`` ``GPU`` ``CPU``

    Examples:
        >>> from mindspore import Tensor, ops
        >>> from mindspore.ops.composite.base import MultitypeFuncGraph, HyperMap
        >>> from mindspore import dtype as mstype
        >>> nest_tensor_list = ((Tensor(1, mstype.float32), Tensor(2, mstype.float32)),
        ...                     (Tensor(3, mstype.float32), Tensor(4, mstype.float32)))
        >>> # square all the tensor in the nested list
        >>>
        >>> square = MultitypeFuncGraph('square')
        >>> @square.register("Tensor")
        ... def square_tensor(x):
        ...     return ops.square(x)
        >>>
        >>> common_map = HyperMap()
        >>> output = common_map(square, nest_tensor_list)
        >>> print(output)
        ((Tensor(shape=[], dtype=Float32, value= 1), Tensor(shape=[], dtype=Float32, value= 4)),
        (Tensor(shape=[], dtype=Float32, value= 9), Tensor(shape=[], dtype=Float32, value= 16)))
        >>> square_map = HyperMap(square, False)
        >>> output = square_map(nest_tensor_list)
        >>> print(output)
        ((Tensor(shape=[], dtype=Float32, value= 1), Tensor(shape=[], dtype=Float32, value= 4)),
        (Tensor(shape=[], dtype=Float32, value= 9), Tensor(shape=[], dtype=Float32, value= 16)))
    """

    def __init__(self, ops=None, reverse=False):
        """Initialize HyperMap."""
        self.ops = ops
        if ops:
            HyperMap_.__init__(self, reverse, ops)
        else:
            HyperMap_.__init__(self, reverse)

    def __call__(self, *args):
        func = self.ops
        args_list = args
        hypermap = self
        if self.ops is None:
            func = args[0]
            args_list = args[1:]
            hypermap = partial(self, func)
        # is leaf
        if not isinstance(args_list[0], (tuple, list)):
            return func(*args_list)
        return tuple(map(hypermap, *args_list))


class Map(Map_):
    """
    Map will apply the set operation on input sequences.

    Apply the operations to every element of the sequence.

    Args:
        ops (Union[MultitypeFuncGraph, None]): `ops` is the operation to apply. If `ops` is `None`,
            the operations should be put in the first input of the instance. Default: None
        reverse (bool): The optimizer needs to be inverted in some scenarios to improve parallel performance,
          general users please ignore. `Reverse` is the flag to decide if apply the operation reversely.
          Only supported in graph mode. Default is False.

    Inputs:
        - **args** (Tuple[sequence]) - If `ops` is not `None`, all the inputs should be the same length sequences,
          and each row of the sequences. e.g. If the length of args is 2, and for `i` in length of each sequence
          `(args[0][i], args[1][i])` will be the input of the operation.

          If `ops` is `None`, the first input is the operation, and the other is inputs.

    Outputs:
        Sequence, the sequence of output after applying the function. e.g. `operation(args[0][i], args[1][i])`.

    Supported Platforms:
        ``Ascend`` ``GPU`` ``CPU``

    Examples:
        >>> from mindspore import dtype as mstype
        >>> from mindspore import Tensor, ops
        >>> from mindspore.ops import MultitypeFuncGraph, Map
        >>> tensor_list = (Tensor(1, mstype.float32), Tensor(2, mstype.float32), Tensor(3, mstype.float32))
        >>> # square all the tensor in the list
        >>>
        >>> square = MultitypeFuncGraph('square')
        >>> @square.register("Tensor")
        ... def square_tensor(x):
        ...     return ops.square(x)
        >>>
        >>> common_map = Map()
        >>> output = common_map(square, tensor_list)
        >>> print(output)
        (Tensor(shape=[], dtype=Float32, value= 1), Tensor(shape=[], dtype=Float32, value= 4),
        Tensor(shape=[], dtype=Float32, value= 9))
        >>> square_map = Map(square, False)
        >>> output = square_map(tensor_list)
        >>> print(output)
        (Tensor(shape=[], dtype=Float32, value= 1), Tensor(shape=[], dtype=Float32, value= 4),
        Tensor(shape=[], dtype=Float32, value= 9))
    """

    def __init__(self, ops=None, reverse=False):
        """Initialize Map."""
        self.ops = ops
        if ops:
            Map_.__init__(self, reverse, ops)
        else:
            Map_.__init__(self, reverse)

    def __call__(self, *args):
        func = self.ops
        args_list = args
        if self.ops is None:
            func = args[0]
            args_list = args[1:]
        return tuple(map(func, *args_list))


class Shard(Shard_):
    """Shard operation"""
    def __init__(self):
        """Initialize Shard."""
        Shard_.__init__(self, 'Shard')
        self.shard_fn = None
        self.fn = None
        self.in_axes = None
        self.out_axes = None
        self.device = None
        self.level = None

    def __call__(self, fn, in_axes, out_axes, device="Ascend", level=0):
        if context.get_context("mode") != context.PYNATIVE_MODE or \
            context.get_auto_parallel_context("parallel_mode") not in ["semi_auto_parallel", "auto_parallel"]:
            raise AssertionError(f"'Shard' only supports semi_auto/auto parallel under PyNative mode")
        if not isinstance(in_axes, tuple):
            raise TypeError(f"For 'Shard', the 'in_axes' should be a tuple, but got {type(in_axes).__name__}")
        if not isinstance(out_axes, tuple):
            raise TypeError(f"For 'Shard', the 'out_axes' should be a tuple, "
                            f"but got {type(out_axes).__name__}")
        if not isinstance(device, str):
            raise TypeError(f"For 'Shard', the 'device' should be a string, "
                            f"but got {type(device).__name__}")
        if not isinstance(level, int):
            raise TypeError(f"For 'Shard', the 'level' should be an integer, "
                            f"but got {type(level).__name__}")
        if self.shard_fn is not None and self.fn == fn and self.in_axes == in_axes and self.out_axes == out_axes and \
            self.device == device and self.level == level:
            return self.shard_fn
        shard_ = Shard()

        @ms_function(obj=fn)
        def after_shard(*args):
            return shard_(fn, in_axes, out_axes, device, level)(*args)

        self.shard_fn = after_shard
        self.fn = fn
        self.in_axes = in_axes
        self.out_axes = out_axes
        self.device = device
        self.level = level
        return self.shard_fn


class _ListAppend(ListAppend_):
    """
    A metafuncgraph class that append one element to list.

    Args:
        name (str): The name of the metafuncgraph object.
    """

    def __init__(self, name):
        """Initialize _ListAppend."""
        ListAppend_.__init__(self, name)

    def __call__(self, *args):
        pass


_append = _ListAppend("append")


class _ListInsert(ListInsert_):
    """
    A metafuncgraph class that insert one element to list.

    Args:
        name (str): The name of the metafuncgraph object.
    """

    def __init__(self, name):
        """Initialize _ListAppend."""
        ListInsert_.__init__(self, name)

    def __call__(self, *args):
        pass


_insert = _ListInsert("insert")


class _Tail(Tail_):
    """
    A metafuncgraph class that generates tail elements of the tuple.

    Args:
        name (str): The name of the metafuncgraph object.
    """

    def __init__(self, name):
        """Initialize _Tail."""
        Tail_.__init__(self, name)

    def __call__(self, *args):
        pass


tail = _Tail('tail')


class _ZipOperation(ZipOperation_):
    """Generates a tuple of zip iterations for inputs."""

    def __init__(self, name):
        """Initialize _ZipOperation."""
        ZipOperation_.__init__(self, name)

    def __call__(self, *args):
        pass


zip_operation = _ZipOperation('zip_operation')
"""`zip_operation` will generate a tuple of zip iterations of inputs."""


env_get = MultitypeFuncGraph("env_get")


environ_get = Primitive('EnvironGet')
ref_to_embed = _grad_ops.RefToEmbed()
zeros_like = P.ZerosLike()


@env_get.register("EnvType", "Tensor")
def _tensor_env_get(env, parameter):
    """Used to get env."""
    return environ_get(env, ref_to_embed(parameter), zeros_like(parameter))
