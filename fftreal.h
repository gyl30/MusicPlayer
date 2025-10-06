/*
 * Copyright (c) 2018 Dmitry V. Benko <d-b-w@yandex.ru>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is

 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __FFTREAL_H
#define __FFTREAL_H

#include <vector>
#include <cmath>
#include <complex>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

template <typename T> class fft_real {
public:

  /**
   * @brief fft_real<T>::fft_real - constructor
   *
   * The constructor prepares the transformation tables.
   *
   * @param[in] length - the transformation length. Must be a power of 2.
   *
   * Note:
   * The constructor is thread-safe. You can create as many fft_real objects as
   * you want.
   */
  fft_real(size_t length) {

    if(length == 0) return;

    if((length & (length - 1)) != 0) {
      // not a power of two
      return;
    }

    m_length = length;
    m_log2_len = 0;
    while((((size_t)1) << m_log2_len) < m_length) {
      m_log2_len++;
    }

    //
    // prepare the transformation tables
    //
    m_rev.resize(m_length);

    //
    // reverse-binary reordering
    //
    for(size_t i = 0; i < m_length; i++) {
      m_rev[i] = reverse(i, m_log2_len);
    }

    //
    // trigonometric tables
    //
    m_cos.resize(m_log2_len);
    m_sin.resize(m_log2_len);

    for(size_t log2_block_size = 1; log2_block_size <= m_log2_len; log2_block_size++) {
      size_t block_size = (size_t)1 << log2_block_size;
      m_cos[log2_block_size-1].resize(block_size/2);
      m_sin[log2_block_size-1].resize(block_size/2);

      for(size_t i = 0; i < block_size/2; i++) {
        double angle = 2.0 * M_PI * (double)i / (double)block_size;
        m_cos[log2_block_size-1][i] = cos(angle);
        m_sin[log2_block_size-1][i] = sin(angle);
      }
    }

    m_initialized = true;
  }

  /**
   * @brief fft_real<T>::get_length
   *
   * @return the transformation length
   */
  size_t get_length() {
    return m_length;
  }

  /**
   * @brief fft_real<T>::get_real
   *
   * @param[in] k - frequency index [0..length/2]
   * @return k-th real part of the transformation result
   *
   * Note:
   * The method should be used ONLY after do_fft() or do_ifft() call.
   * Returned value has no warranty to be valid otherwise.
   */
  T get_real(size_t k) {
    if(!m_initialized) return 0;

    if(k > m_length/2) {
      return 0; // out of range
    }

    if(k==0) {
      return m_x[0];
    } else if(k < m_length/2) {
      return m_x[k];
    } else { // k == m_length/2
      return m_x[m_length-1];
    }
  }


  /**
   * @brief fft_real<T>::get_imag
   *
   * @param[in] k - frequency index [0..length/2]
   * @return k-th imaginary part of the transformation result
   *
   * Note:
   * The method should be used ONLY after do_fft() or do_ifft() call.
   * Returned value has no warranty to be valid otherwise.
   */
  T get_imag(size_t k) {
    if(!m_initialized) return 0;

    if(k > m_length/2) {
      return 0; // out of range
    }

    if(k==0 || k == m_length/2) {
      return 0;
    } else {
      return m_x[m_length-k];
    }
  }

  /**
   * @brief fft_real<T>::get_spectrum
   *
   * @param[out] spectrum - a pointer to an array of complex<T> of size
   *                        length/2+1
   *
   * Note:
   * The method should be used ONLY after do_fft() or do_ifft() call.
   * Returned value has no warranty to be valid otherwise.
   */
  void get_spectrum(std::complex<T> * spectrum) {
    if(!m_initialized || !spectrum) return;

    for(size_t k=0; k<=m_length/2; k++) {
      spectrum[k].real(get_real(k));
      spectrum[k].imag(get_imag(k));
    }
  }

  /**
   * @brief fft_real<T>::do_fft - perform the forward FFT
   *
   * Performs the forward FFT of a real-valued signal.
   *
   * @param[in,out] data - input data array (time domain).
   *                       The array size must be equal to the transformation
   *                       length.
   *                       The array is used as a workspace and WILL BE
   *                       MODIFIED.
   *
   * Note:
   * The method is NOT thread safe itself. If you want to use it from a
   * multithreaded application, you have to use a separate fft_real object for
   * each thread or protect the calls with a mutex.
   */
  void do_fft(T * data) {
    if(!m_initialized || !data) return;

    //
    // 1. complex FFT of a real-valued signal
    //
    do_complex_fft(data, false);

    //
    // 2. post-processing
    //
    for (size_t k = 1; k < m_length/2; k++) {
      T re_k = m_x[k];
      T im_k = m_x[m_length-k];
      T re_nk = m_x[m_length-k];
      T im_nk = m_x[k];

      m_x[k] = (re_k+re_nk)/2.0;
      m_x[m_length-k] = (im_k-im_nk)/2.0;
    }

    // k = 0
    m_x[0] = m_x[0];
    // k = length/2
    m_x[m_length-1] = m_x[m_length/2];
  }


  /**
   * @brief fft_real<T>::do_ifft - perform the inverse FFT
   *
   * Performs the inverse FFT. The method restores the real-valued signal from
   * a frequency spectrum.
   * The spectrum is passed as an array of complex numbers of size length/2+1.
   *
   * @param[in] spectrum - the spectrum array.
   * @param[out] data - output data array. Its size MUST be equal to the
   *                    transformation length.
   *
   * Note:
   * The method is NOT thread safe itself. If you want to use it from a
   * multithreaded application, you have to use a separate fft_real object for
   * each thread or protect the calls with a mutex.
   */
  void do_ifft(const std::complex<T> * spectrum, T * data) {
    if(!m_initialized || !spectrum || !data) return;

    //
    // 1. pre-processing
    //
    m_x.resize(m_length);
    for (size_t k = 1; k < m_length/2; k++) {
      m_x[k]           = spectrum[k].real() - spectrum[k].imag();
      m_x[m_length-k]  = spectrum[k].real() + spectrum[k].imag();
    }

    // k = 0
    m_x[0] = spectrum[0].real();
    // k = length/2
    m_x[m_length/2] = spectrum[m_length/2].real();

    //
    // 2. complex FFT
    //
    do_complex_fft(m_x.data(), true);

    //
    // 3. store the result
    //
    for(size_t i=0; i<m_length; i++) {
      data[i] = m_x[i];
    }

  }

private:

  void do_complex_fft(T* data, bool inverse) {
    if(!m_initialized || !data) return;

    //
    // 1. reverse-binary reordering
    //
    m_x.resize(m_length);
    for(size_t i = 0; i < m_length; i++) {
      m_x[i] = data[m_rev[i]];
    }

    //
    // 2. Danielson-Lanczos
    //
    for(size_t log2_block_size = 1; log2_block_size <= m_log2_len; log2_block_size++) {
      size_t block_size = (size_t)1 << log2_block_size;
      size_t num_blocks = m_length / block_size;

      T * p_cos = m_cos[log2_block_size-1].data();
      T * p_sin = m_sin[log2_block_size-1].data();

      for(size_t block_idx = 0; block_idx < num_blocks; block_idx++) {
        size_t block_start = block_idx * block_size;
        for(size_t i = 0; i < block_size/2; i++) {
          size_t i1 = block_start+i;
          size_t i2 = i1 + block_size/2;

          T cos_ = p_cos[i];
          T sin_ = p_sin[i];
          if(inverse) {
            sin_ = -sin_;
          }

          std::complex<T> arg1(m_x[i1], m_x[m_length-1-i1]);
          std::complex<T> arg2(m_x[i2], m_x[m_length-1-i2]);
          std::complex<T> w(cos_, sin_);

          std::complex<T> term = w * arg2;

          std::complex<T> res1 = arg1 + term;
          std::complex<T> res2 = arg1 - term;

          m_x[i1] = res1.real(); m_x[m_length-1-i1] = res1.imag();
          m_x[i2] = res2.real(); m_x[m_length-1-i2] = res2.imag();
        }
      }
    }

    //
    // 3. scaling
    //
    if(inverse) {
      for(size_t i=0; i<m_length; i++) {
        m_x[i] /= (T)m_length;
      }
    }
  }

  size_t reverse(size_t x, size_t n) {
    size_t res = 0;
    for (size_t i = 0; i < n; i++) {
      if ((x >> i) & 1) {
        res |= (size_t)1 << (n - 1 - i);
      }
    }
    return res;
  }

  bool                        m_initialized = false;
  size_t                      m_length = 0;
  size_t                      m_log2_len = 0;

  std::vector<size_t>         m_rev;        // reverse-binary table
  std::vector<T>              m_x;          // workspace

  // trigonometric tables
  std::vector<std::vector<T>> m_cos;
  std::vector<std::vector<T>> m_sin;
};


#endif /* __FFTREAL_H */
