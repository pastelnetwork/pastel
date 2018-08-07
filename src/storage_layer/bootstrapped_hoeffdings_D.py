import time, functools, random
import scipy
from scipy.stats import rankdata
import numpy as np
import concurrent.futures as cf
from multiprocessing import Pool, cpu_count
pool = Pool(int(round(cpu_count()/2)))
use_demonstrate_hoeffding = 1

class MyTimer():
    def __init__(self):
        self.start = time.time()
    def __enter__(self):
        return self
    def __exit__(self, exc_type, exc_val, exc_tb):
        end = time.time()
        runtime = end - self.start
        msg = '({time} seconds to complete)'
        print(msg.format(time=round(runtime,5)))

def hoeffd_inner_loop_func(i, R, S):
    # See slow_exact_hoeffdings_d_func for definition of R, S
    Q_i = 1 + sum(np.logical_and(R<R[i], S<S[i]))
    Q_i = Q_i + (1/4)*(sum(np.logical_and(R==R[i], S==S[i])) - 1)
    Q_i = Q_i + (1/2)*sum(np.logical_and(R==R[i], S<S[i]))
    Q_i = Q_i + (1/2)*sum(np.logical_and(R<R[i], S==S[i]))
    return Q_i

def slow_exact_hoeffdings_d_func(x, y, pool):
    #Based on code from here: https://stackoverflow.com/a/9322657/1006379
    #For background see: https://projecteuclid.org/download/pdf_1/euclid.aoms/1177730150
    x = np.array(x)
    y = np.array(y)
    N = x.shape[0]
    R = scipy.stats.rankdata(x, method='average')
    S = scipy.stats.rankdata(y, method='average')
    if 0:
        print('Computing Q with list comprehension...')
        with MyTimer():
            Q = [hoeffd_inner_loop_func(i, R, S) for i in range(N)]
    if 1:
        print('Computing Q with multiprocessing...')
        with MyTimer():
            hoeffd = functools.partial(hoeffd_inner_loop_func, R=R, S=S)
            Q = pool.map(hoeffd, range(N))
    Q = np.array(Q)
    D1 = sum(((Q-1)*(Q-2)))
    D2 = sum((R-1)*(R-2)*(S-1)*(S-2))
    D3 = sum((R-2)*(S-2)*(Q-1))
    D = 30*((N-2)*(N-3)*D1 + D2 - 2*(N-2)*D3) / (N*(N-1)*(N-2)*(N-3)*(N-4))
    print('Exact Hoeffding D: '+ str(round(D,6)))
    return D

def generate_bootstrap_sample_func(original_length_of_input, sample_size):
    bootstrap_indices = np.array([random.randint(1,original_length_of_input) for x in range(sample_size)])
    return bootstrap_indices

def compute_average_and_stdev_of_25th_to_75th_percentile_func(input_vector):
    input_vector = np.array(input_vector)
    percentile_25 = np.percentile(input_vector, 25)
    percentile_75 = np.percentile(input_vector, 75)
    trimmed_vector = input_vector[input_vector>percentile_25]
    trimmed_vector = trimmed_vector[trimmed_vector<percentile_75]
    trimmed_vector_avg = np.mean(trimmed_vector)
    trimmed_vector_stdev = np.std(trimmed_vector)
    return trimmed_vector_avg, trimmed_vector_stdev

def compute_bootstrapped_hoeffdings_d_func(x, y, pool, sample_size):
    x = np.array(x)
    y = np.array(y)
    assert(x.size==y.size)
    original_length_of_input = x.size
    bootstrap_sample_indices = generate_bootstrap_sample_func(original_length_of_input-1, sample_size)
    N = sample_size
    x_bootstrap_sample = x[bootstrap_sample_indices]
    y_bootstrap_sample = y[bootstrap_sample_indices]
    R_bootstrap = scipy.stats.rankdata(x_bootstrap_sample)
    S_bootstrap = scipy.stats.rankdata(y_bootstrap_sample)
    hoeffdingd = functools.partial(hoeffd_inner_loop_func, R=R_bootstrap, S=S_bootstrap)
    Q_bootstrap = pool.map(hoeffdingd, range(sample_size))
    Q = np.array(Q_bootstrap)
    D1 = sum(((Q-1)*(Q-2)))
    D2 = sum((R_bootstrap-1)*(R_bootstrap-2)*(S_bootstrap-1)*(S_bootstrap-2))
    D3 = sum((R_bootstrap-2)*(S_bootstrap-2)*(Q-1))
    D = 30*((N-2)*(N-3)*D1 + D2 - 2*(N-2)*D3) / (N*(N-1)*(N-2)*(N-3)*(N-4))
    return D

def compute_parallel_bootstrapped_bagged_hoeffdings_d_func(x, y, sample_size, number_of_bootstraps, pool):
    def apply_bootstrap_hoeffd_func(ii):
        verbose = 0
        if verbose:
            print('Bootstrap '+str(ii) +' started...')
        return compute_bootstrapped_hoeffdings_d_func(x, y, pool, sample_size)
    list_of_Ds = list()
    with cf.ThreadPoolExecutor() as executor:
        inputs = range(number_of_bootstraps)
        for result in executor.map(apply_bootstrap_hoeffd_func, inputs):
            list_of_Ds.append(result)
    robust_average_D, robust_stdev_D = compute_average_and_stdev_of_25th_to_75th_percentile_func(list_of_Ds)
    return list_of_Ds, robust_average_D, robust_stdev_D


if use_demonstrate_hoeffding:
    
    if 0: #Should be: exact_D_test  = 0.313988 #Source: http://reference.wolfram.com/language/example/UseHoeffdingsDToQuantifyAndTestNonMonotonicDependence.html
        test_vector_x = [3, -4, 1, 4, 22, 17, -2, 2, 13, -11]
        test_vector_y = [-20, -24, 0, 4, 24, 36, -12, -12, 56, -14]
        x = test_vector_x
        y = test_vector_y
        exact_D_test = slow_exact_hoeffdings_d_func(test_vector_x, test_vector_y, pool)
    
    if 0: #Demonstration of D for various joint distributions:
        test_vector_x =  list(np.random.uniform(0,500,3000))
        test_vector_y_independent =  list(np.random.uniform(0,500,3000))
        test_vector_y_linear = [x*5+0.2*random.random() for x in test_vector_x]
        test_vector_y_quadratic = [x*x+random.random() for x in test_vector_x]
        test_vector_y_sine = [np.sin(x) for x in test_vector_x]
    
        exact_D_same = slow_exact_hoeffdings_d_func(test_vector_x, test_vector_x, pool)
        exact_D_independent = slow_exact_hoeffdings_d_func(test_vector_x, test_vector_y_independent, pool)
        exact_D_linear = slow_exact_hoeffdings_d_func(test_vector_x, test_vector_y_linear, pool)
        exact_D_quadratic = slow_exact_hoeffdings_d_func(test_vector_x, test_vector_y_quadratic, pool)

        sample_size = 80
        number_of_bootstraps = 50
        _, robust_average_D_same, _ = compute_parallel_bootstrapped_bagged_hoeffdings_d_func(test_vector_x, test_vector_x, sample_size, number_of_bootstraps, pool)
        _, robust_average_D_independent, _ = compute_parallel_bootstrapped_bagged_hoeffdings_d_func(test_vector_x, test_vector_y_independent, sample_size, number_of_bootstraps, pool)
        _, robust_average_D_linear, _ = compute_parallel_bootstrapped_bagged_hoeffdings_d_func(test_vector_x, test_vector_y_linear, sample_size, number_of_bootstraps, pool)
        _, robust_average_D_quadratic, _ = compute_parallel_bootstrapped_bagged_hoeffdings_d_func(test_vector_x, test_vector_y_quadratic, sample_size, number_of_bootstraps, pool)

        D_same__squared_error = (robust_average_D_same - exact_D_same)**2
        D_independent__squared_error = (robust_average_D_independent - exact_D_independent)**2
        D_linear__squared_error = (robust_average_D_linear - exact_D_linear)**2
        D_quadratic__squared_error = (robust_average_D_quadratic - exact_D_quadratic)**2
        average_squared_error = np.mean(np.array([D_same__squared_error, D_independent__squared_error, D_linear__squared_error, D_quadratic__squared_error]))
        print('Average Squared Error of Bootstrapped Estimates of D versus exact D: ' + str(average_squared_error))


        #Experiment to find most efficient sample size vs bootstrap number:
        list_of_sample_sizes = range(50,400,5)
        list_of_number_of_bootstraps = range(5, 100, 5)
        list_of_average_squared_errors = list()
        list_of_calculation_durations = list()
        
        for sample_size in list_of_sample_sizes:
            for number_of_bootstraps in list_of_number_of_bootstraps:
                start_time = time.time()
                print('Sample Size: ' + str(sample_size) + '; Number of Bootstraps: ' + str(number_of_bootstraps))
                _, robust_average_D_same, _ = compute_parallel_bootstrapped_bagged_hoeffdings_d_func(test_vector_x, test_vector_x, sample_size, number_of_bootstraps, pool)
                _, robust_average_D_independent, _ = compute_parallel_bootstrapped_bagged_hoeffdings_d_func(test_vector_x, test_vector_y_independent, sample_size, number_of_bootstraps, pool)
                _, robust_average_D_linear, _ = compute_parallel_bootstrapped_bagged_hoeffdings_d_func(test_vector_x, test_vector_y_linear, sample_size, number_of_bootstraps, pool)
                _, robust_average_D_quadratic, _ = compute_parallel_bootstrapped_bagged_hoeffdings_d_func(test_vector_x, test_vector_y_quadratic, sample_size, number_of_bootstraps, pool)
        
                D_same__squared_error = (robust_average_D_same - exact_D_same)**2
                D_independent__squared_error = (robust_average_D_independent - exact_D_independent)**2
                D_linear__squared_error = (robust_average_D_linear - exact_D_linear)**2
                D_quadratic__squared_error = (robust_average_D_quadratic - exact_D_quadratic)**2
                average_squared_error = np.mean(np.array([D_same__squared_error, D_independent__squared_error, D_linear__squared_error, D_quadratic__squared_error]))
                end_time = time.time()
                duration_in_seconds = end_time - start_time
                list_of_calculation_durations.append(duration_in_seconds)
                list_of_average_squared_errors.append(average_squared_error)
                print('Average Squared Error of Bootstrapped Estimates of D versus exact D: ' + str(average_squared_error))
                
        