#ifndef INRIA_LDLT_UTIL_HPP_ZX9HNY5GS
#define INRIA_LDLT_UTIL_HPP_ZX9HNY5GS

#include <Eigen/Core>
#include <Eigen/SparseCore>
#include <Eigen/Cholesky>
#include <Eigen/Eigenvalues>
#include <Eigen/QR>
#include <utility>
#include <ldlt/views.hpp>
#include <qp/views.hpp>
#include <ldlt/detail/meta.hpp>
#include <map>

using c_int = long long;
using c_float = double;

template <typename T, ldlt::Layout L>
using Mat = Eigen::Matrix<
		T,
		Eigen::Dynamic,
		Eigen::Dynamic,
		(L == ldlt::colmajor) ? Eigen::ColMajor : Eigen::RowMajor>;
template <typename T>
using Vec = Eigen::Matrix<T, Eigen::Dynamic, 1>;

template <typename Scalar>
using SparseMat = Eigen::SparseMatrix<Scalar, Eigen::ColMajor, c_int>;

namespace ldlt_test {
using namespace ldlt;
namespace eigen {
template <typename T>
void llt_compute( //
		Eigen::LLT<T>& out,
		T const& mat) {
	out.compute(mat);
}
template <typename T>
void ldlt_compute( //
		Eigen::LDLT<T>& out,
		T const& mat) {
	out.compute(mat);
}
LDLT_EXPLICIT_TPL_DECL(2, llt_compute<Mat<f32, colmajor>>);
LDLT_EXPLICIT_TPL_DECL(2, ldlt_compute<Mat<f32, colmajor>>);
LDLT_EXPLICIT_TPL_DECL(2, llt_compute<Mat<f32, rowmajor>>);
LDLT_EXPLICIT_TPL_DECL(2, ldlt_compute<Mat<f32, rowmajor>>);

LDLT_EXPLICIT_TPL_DECL(2, llt_compute<Mat<f64, colmajor>>);
LDLT_EXPLICIT_TPL_DECL(2, ldlt_compute<Mat<f64, colmajor>>);
LDLT_EXPLICIT_TPL_DECL(2, llt_compute<Mat<f64, rowmajor>>);
LDLT_EXPLICIT_TPL_DECL(2, ldlt_compute<Mat<f64, rowmajor>>);
} // namespace eigen
namespace rand {

using ldlt::u64;
using ldlt::u32;
using u128 = __uint128_t;

constexpr u128 lehmer64_constant(0xda942042e4dd58b5);
inline auto lehmer_global() -> u128& {
	static u128 g_lehmer64_state = lehmer64_constant * lehmer64_constant;
	return g_lehmer64_state;
}

inline auto lehmer64() -> u64 { // [0, 2^64)
	lehmer_global() *= lehmer64_constant;
	return u64(lehmer_global() >> u128(64U));
}

inline void set_seed(u64 seed) {
	lehmer_global() = u128(seed) + 1;
	lehmer64();
	lehmer64();
}

inline auto uniform_rand() -> double { // [0, 2^53]
	u64 a = lehmer64() / (1U << 11U);
	return double(a) / double(u64(1) << 53U);
}
inline auto normal_rand() -> double {
	static const double pi2 = std::atan(static_cast<double>(1)) * 8;

	double u1 = uniform_rand();
	double u2 = uniform_rand();

	double ln = std::log(u1);
	double sqrt = std::sqrt(-2 * ln);

	return sqrt * std::cos(pi2 * u2);
}

template <typename Scalar>
auto vector_rand(isize nrows) -> Vec<Scalar> {
	auto v = Vec<Scalar>(nrows);

	for (isize i = 0; i < nrows; ++i) {
		v(i) = Scalar(rand::normal_rand());
	}

	return v;
}
template <typename Scalar>
auto matrix_rand(isize nrows, isize ncols) -> Mat<Scalar, colmajor> {
	auto v = Mat<Scalar, colmajor>(nrows, ncols);

	for (isize i = 0; i < nrows; ++i) {
		for (isize j = 0; j < ncols; ++j) {
			v(i, j) = Scalar(rand::normal_rand());
		}
	}

	return v;
}

namespace detail {
template <typename Scalar>
auto orthonormal_rand_impl(isize n) -> Mat<Scalar, colmajor> {
	auto mat = rand::matrix_rand<Scalar>(n, n);
	auto qr = mat.householderQr();
	Mat<Scalar, colmajor> q = qr.householderQ();
	return q;
}
using Input = std::pair<u128, isize>;
} // namespace detail

template <typename Scalar>
auto orthonormal_rand(isize n) -> Mat<Scalar, colmajor> const& {

	static auto cache = std::map<detail::Input, Mat<Scalar, colmajor>>{};
	auto input = detail::Input{lehmer_global(), n};
	auto it = cache.find(input);
	if (it == cache.end()) {
		auto res = cache.insert({
				input,
				detail::orthonormal_rand_impl<Scalar>(n),
		});
		it = res.first;
	}
	return (*it).second;
}

template <typename Scalar>
auto positive_definite_rand(isize n, Scalar cond) -> Mat<Scalar, colmajor> {
	auto const& q = rand::orthonormal_rand<Scalar>(n);
	auto d = Vec<Scalar>(n);

	{
		using std::exp;
		using std::log;
		Scalar diff = log(cond);
		for (isize i = 0; i < n; ++i) {
			d(i) = exp(Scalar(i) / Scalar(n) * diff);
		}
	}

	return q * d.asDiagonal() * q.transpose();
}

template <typename Scalar>
auto sparse_positive_definite_rand(isize n, Scalar cond, double p)
		-> SparseMat<Scalar> {
	auto H = SparseMat<Scalar>(n, n);

	for (isize i = 0; i < n; ++i) {
		auto urandom = rand::uniform_rand();
		if (urandom < p) {
			auto random = Scalar(rand::normal_rand());
			H.insert(i, i) = random;
		}
	}

	for (isize i = 0; i < n; ++i) {
		for (isize j = i + 1; j < n; ++j) {
			auto urandom = rand::uniform_rand();
			if (urandom < p / 2) {
				auto random = Scalar(rand::normal_rand());
				H.insert(i, j) = random;
			}
		}
	}

	Mat<Scalar, colmajor> H_dense = H.toDense();
	Vec<Scalar> eigh =
			H_dense.template selfadjointView<Eigen::Upper>().eigenvalues();

	Scalar min = eigh.minCoeff();
	Scalar max = eigh.maxCoeff();

	// new_min = min + rho
	// new_max = max + rho
	//
	// (max + rho)/(min + rho) = cond
	// 1 + (max - min) / (min + rho) = cond
	// (max - min) / (min + rho) = cond - 1
	// min + rho = (max - min) / (cond - 1)
	// rho = (max - min)/(cond - 1) - min
	Scalar rho = (max - min) / (cond - 1) - min;

	for (isize i = 0; i < n; ++i) {
		H.coeffRef(i, i) += rho;
	}
	H.makeCompressed();
	return H;
}

template <typename Scalar>
auto sparse_positive_definite_rand_not_compressed(isize n, Scalar rho, double p)
		->  Mat<Scalar, colmajor>  {
	auto H = Mat<Scalar, colmajor>(n, n);
	H.setZero();

	for (isize i = 0; i < n; ++i) {
		for (isize j = 0; j < n; ++j) {
			auto urandom = rand::uniform_rand();
			if (urandom < p) {
				auto random = Scalar(rand::normal_rand());
				H(i, j) = random;
			}
		}
	}
	
	H = H * H.transpose(); // safe no aliasing https://eigen.tuxfamily.org/dox/group__TopicAliasing.html
	H.diagonal().array() += rho;

	return H;
}

template <typename Scalar>
auto sparse_matrix_rand(isize nrows, isize ncols, double p)
		-> SparseMat<Scalar> {
	auto A = SparseMat<Scalar>(nrows, ncols);

	for (isize i = 0; i < nrows; ++i) {
		for (isize j = 0; j < ncols; ++j) {
			if (rand::uniform_rand() < p) {
				A.insert(i, j) = Scalar(rand::normal_rand());
			}
		}
	}
	A.makeCompressed();
	return A;
}


template <typename Scalar>
auto sparse_matrix_rand_not_compressed(isize nrows, isize ncols, double p)
		->  Mat<Scalar, colmajor>{
	auto A =  Mat<Scalar, colmajor>(nrows, ncols);
	A.setZero();
	for (isize i = 0; i < nrows; ++i) {
		for (isize j = 0; j < ncols; ++j) {
			if (rand::uniform_rand() < p) {
				A(i, j) = Scalar(rand::normal_rand());
			}
		}
	}
	return A;
}

LDLT_EXPLICIT_TPL_DECL(2, matrix_rand<f32>);
LDLT_EXPLICIT_TPL_DECL(1, vector_rand<f32>);
LDLT_EXPLICIT_TPL_DECL(2, positive_definite_rand<f32>);
LDLT_EXPLICIT_TPL_DECL(1, orthonormal_rand<f32>);
LDLT_EXPLICIT_TPL_DECL(3, sparse_matrix_rand<f32>);
LDLT_EXPLICIT_TPL_DECL(3, sparse_positive_definite_rand<f32>);

LDLT_EXPLICIT_TPL_DECL(2, matrix_rand<f64>);
LDLT_EXPLICIT_TPL_DECL(1, vector_rand<f64>);
LDLT_EXPLICIT_TPL_DECL(2, positive_definite_rand<f64>);
LDLT_EXPLICIT_TPL_DECL(1, orthonormal_rand<f64>);
LDLT_EXPLICIT_TPL_DECL(3, sparse_matrix_rand<f64>);
LDLT_EXPLICIT_TPL_DECL(3, sparse_positive_definite_rand<f64>);
} // namespace rand
using ldlt::detail::Duration;
using ldlt::detail::Clock;
using ldlt::usize;

template <typename T>
struct BenchResult {
	Duration duration;
	T result;
};

template <typename Fn>
auto bench_for_n(usize n, Fn fn) -> BenchResult<decltype(fn())> {
	auto begin = Clock::now();

	auto result = fn();

	for (usize i = 1; i < n; ++i) {
		result = fn();
	}

	auto end = Clock::now();
	return {
			(end - begin) / n,
			LDLT_FWD(result),
	};
}

template <typename Fn>
auto bench_for(Duration d, Fn fn) -> BenchResult<decltype(fn())> {
	namespace time = std::chrono;
	using DurationF64 = time::duration<double, std::ratio<1>>;

	auto elapsed = Duration(0);
	usize n_runs = 1;

	while (true) {
		auto res = ldlt_test::bench_for_n(n_runs, fn);
		elapsed = res.duration;
		if (elapsed > d) {
			return res;
		}

		if ((elapsed * n_runs) > time::microseconds{100}) {
			break;
		}
		n_runs *= 2;
	}

	auto d_f64 = time::duration_cast<DurationF64>(d);
	auto elapsed_f64 = time::duration_cast<DurationF64>(elapsed);
	double ratio = (d_f64 / elapsed_f64);
	return ldlt_test::bench_for_n(usize(std::ceil(ratio)), fn);
}

namespace osqp {
auto to_sparse(Mat<c_float, colmajor> const& mat) -> SparseMat<c_float>;
auto to_sparse_sym(Mat<c_float, colmajor> const& mat) -> SparseMat<c_float>;
} // namespace osqp
} // namespace ldlt_test

template <typename T>
auto matmul_impl( //
		Mat<T, ldlt::colmajor> const& lhs,
		Mat<T, ldlt::colmajor> const& rhs) -> Mat<T, ldlt::colmajor> {
	return lhs.operator*(rhs);
}
template <typename To, typename From>
auto mat_cast(Mat<From, ldlt::colmajor> const& from)
		-> Mat<To, ldlt::colmajor> {
	return from.template cast<To>();
}
LDLT_EXPLICIT_TPL_DECL(2, matmul_impl<long double>);
LDLT_EXPLICIT_TPL_DECL(1, mat_cast<ldlt::f64, long double>);
LDLT_EXPLICIT_TPL_DECL(1, mat_cast<ldlt::f32, long double>);

template <
		typename MatLhs,
		typename MatRhs,
		typename T = typename MatLhs::Scalar>
auto matmul(MatLhs const& a, MatRhs const& b) -> Mat<T, ldlt::colmajor> {
	using Upscaled = typename std::
			conditional<std::is_floating_point<T>::value, long double, T>::type;

	return ::mat_cast<T, Upscaled>(::matmul_impl<Upscaled>(
			Mat<T, ldlt::colmajor>(a).template cast<Upscaled>(),
			Mat<T, ldlt::colmajor>(b).template cast<Upscaled>()));
}

template <
		typename MatLhs,
		typename MatMid,
		typename MatRhs,
		typename T = typename MatLhs::Scalar>
auto matmul3(MatLhs const& a, MatMid const& b, MatRhs const& c)
		-> Mat<T, ldlt::colmajor> {
	return ::matmul(::matmul(a, b), c);
}

namespace ldlt {
namespace detail {
template <typename... Ts>
using type_sequence = meta_::type_sequence<Ts...>*;

template <typename T>
struct type_list_ith_elem_impl;

template <usize I, typename List>
using typeseq_ith =
		typename type_list_ith_elem_impl<List>::template ith_elem<I>;

template <typename... Ts>
struct type_list_ith_elem_impl<type_sequence<Ts...>> {
	template <usize I>
	using ith_elem = ith<I, Ts...>;
};
template <typename T, T Val>
struct constant {
	static constexpr T value = Val;
};
} // namespace detail
} // namespace ldlt

LDLT_DEFINE_TAG(random_with_dim_and_n_eq, RandomWithDimAndNeq);
LDLT_DEFINE_TAG(random_with_dim_and_n_in, RandomWithDimAndNin);
LDLT_DEFINE_TAG(from_data, FromData);
template <typename Scalar>
struct Qp {
	Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor> H;
	Eigen::Matrix<Scalar, Eigen::Dynamic, 1, Eigen::ColMajor> g;
	Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor> A;
	Eigen::Matrix<Scalar, Eigen::Dynamic, 1, Eigen::ColMajor> b;
	Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor> C;
	Eigen::Matrix<Scalar, Eigen::Dynamic, 1, Eigen::ColMajor> u;
	Eigen::Matrix<Scalar, Eigen::Dynamic, 1, Eigen::ColMajor> l;

	Eigen::Matrix<Scalar, Eigen::Dynamic, 1> solution;

	Qp(FromData /*tag*/,
	   Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor> H_,
	   Eigen::Matrix<Scalar, Eigen::Dynamic, 1, Eigen::ColMajor> g_,
	   Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor> A_,
	   Eigen::Matrix<Scalar, Eigen::Dynamic, 1, Eigen::ColMajor> b_,
	   Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor> C_,
	   Eigen::Matrix<Scalar, Eigen::Dynamic, 1, Eigen::ColMajor> u_,
	   Eigen::Matrix<Scalar, Eigen::Dynamic, 1, Eigen::ColMajor> l_
	   ) noexcept
			: H(LDLT_FWD(H_)),
				g(LDLT_FWD(g_)),
				A(LDLT_FWD(A_)),
				b(LDLT_FWD(b_)),
				C(LDLT_FWD(C_)),
				u(LDLT_FWD(u_)),
				l(LDLT_FWD(l_)),
				solution(H.rows() + A.rows() + C.rows()) {

		/*
		ldlt::isize dim = ldlt::isize(H.rows());
		ldlt::isize n_eq = ldlt::isize(A.rows());

		auto kkt_mat =
				Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor>(
						dim + n_eq, dim + n_eq);

		kkt_mat.topLeftCorner(dim, dim) = H;
		kkt_mat.topRightCorner(dim, n_eq) = A.transpose();
		kkt_mat.bottomLeftCorner(n_eq, dim) = A;
		kkt_mat.bottomRightCorner(n_eq, n_eq).setZero();

		solution.topRows(dim) = -g;
		solution.bottomRows(n_eq) = b;
		kkt_mat.ldlt().solveInPlace(solution);
		*/
	}

	Qp(RandomWithDimAndNeq /*tag*/, ldlt::isize dim, ldlt::isize n_eq)
			: H(ldlt_test::rand::positive_definite_rand<Scalar>(dim, Scalar(1e2))),
				g(dim),
				A(ldlt_test::rand::matrix_rand<Scalar>(n_eq, dim)),
				b(n_eq),
				solution(ldlt_test::rand::vector_rand<Scalar>(dim + n_eq)) {

		// 1/2 (x-sol)T H (x-sol)
		// 1/2 xT H x - (H sol).T x
		auto primal_solution = solution.topRows(dim);
		auto dual_solution = solution.bottomRows(n_eq);

		g = -H * primal_solution - A.transpose() * dual_solution;
		b = A * primal_solution;
	}

	Qp(RandomWithDimAndNin /*tag*/, ldlt::isize dim, double sparsity_factor)
			: H(ldlt_test::rand::sparse_positive_definite_rand_not_compressed<Scalar>(dim, Scalar(1e-2), sparsity_factor)),
			  g(ldlt_test::rand::vector_rand<Scalar>(dim)),
			  A(ldlt_test::rand::sparse_matrix_rand_not_compressed<Scalar>(0, dim, sparsity_factor)),
			  b(0),
			  C(ldlt_test::rand::sparse_matrix_rand_not_compressed<Scalar>(ldlt::isize(dim/2), dim, sparsity_factor)),
			  u(ldlt::isize(dim/2)),
			  l(ldlt::isize(dim/2)) {
	
			ldlt::isize n_in = ldlt::isize(dim/2);
			
			auto x_sol = ldlt_test::rand::vector_rand<Scalar>(dim);
			auto delta = Vec<Scalar>(n_in);

			for (ldlt::isize i = 0; i < n_in; ++i) {
				delta(i) = ldlt_test::rand::uniform_rand();
			}

			u = C * x_sol + delta ;
			l.setZero();
			l.array() -= 1.e30;
			/*
			sparsity = (n**2*sparsity_factor + 2 * n * n_in*sparsity_factor +  n_in) / (n+n_in)**2

			H_ = sparse.random(n, n, density=sparsity_factor,
							data_rvs=np.random.randn,
							format='csc')
			H_ = H_.dot(H_.T).tocsc() + 1e-02 * sparse.eye(n)
			g_ = np.random.randn(n)
			C_ = sparse.random(n_in, n, density=sparsity_factor,
									data_rvs=np.random.randn,
									format='csc')
			x_sol = np.random.randn(n)  # Create fictitious solution
			delta = np.random.rand(n_in)
			u_ = C_@x_sol + delta
			#l_ = - np.inf * np.ones(n_in)
			l_ = - 1.e30 * np.ones(n_in)
			*/

	}

	auto as_view() -> qp::QpView<Scalar> {
		return {
				{ldlt::from_eigen, H},
				{ldlt::from_eigen, g},
				{ldlt::from_eigen, A},
				{ldlt::from_eigen, b},
				{ldlt::from_ptr_rows_cols_stride, nullptr, 0, ldlt::isize(H.rows()), 0},
				{ldlt::from_ptr_size, nullptr, 0},
		};
	}
	auto as_mut() -> qp::QpViewMut<Scalar> {
		return {
				{ldlt::from_eigen, H},
				{ldlt::from_eigen, g},
				{ldlt::from_eigen, A},
				{ldlt::from_eigen, b},
				{ldlt::from_ptr_rows_cols_stride, nullptr, 0, ldlt::isize(H.rows()), 0},
				{ldlt::from_ptr_size, nullptr, 0},
		};
	}
};

struct EigenNoAlloc {
	EigenNoAlloc(EigenNoAlloc&&) = delete;
	EigenNoAlloc(EigenNoAlloc const&) = delete;
	auto operator=(EigenNoAlloc&&) -> EigenNoAlloc& = delete;
	auto operator=(EigenNoAlloc const&) -> EigenNoAlloc& = delete;

#if defined(EIGEN_RUNTIME_NO_MALLOC)
	EigenNoAlloc() noexcept { Eigen::internal::set_is_malloc_allowed(false); }
	~EigenNoAlloc() noexcept { Eigen::internal::set_is_malloc_allowed(true); }
#else
	EigenNoAlloc() = default;
#endif
};

#endif /* end of include guard INRIA_LDLT_UTIL_HPP_ZX9HNY5GS */
