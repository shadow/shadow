/// An object to encode the window scaling logic. It asserts that no operations have been taken
/// out-of-order.
#[derive(Copy, Clone, Debug)]
pub(crate) struct WindowScaling {
    sent_syn: bool,
    received_syn: bool,
    recv_window_scale_shift: Option<u8>,
    send_window_scale_shift: Option<u8>,
    disabled: bool,
}

impl WindowScaling {
    /// Maximum value of the "Window Scale" TCP option as specified by RFC 7323.
    const MAX_SCALE_SHIFT: u8 = 14;

    pub fn new() -> Self {
        Self {
            sent_syn: false,
            received_syn: false,
            recv_window_scale_shift: None,
            send_window_scale_shift: None,
            disabled: false,
        }
    }

    /// Given a maximum window size, this returns the window scale shift that should be used. As the
    /// window size must be less than 2^30, the returned shift will be limited to 14. This does not
    /// guarantee that `window >> shift` fits within a `u16`.
    pub fn scale_shift_for_max_window(window: u32) -> u8 {
        // the maximum possible window allowed by tcp
        let max_window_size = (u16::MAX as u32) << Self::MAX_SCALE_SHIFT;

        // calculation based on linux 6.5 `tcp_select_initial_window`:
        // https://elixir.bootlin.com/linux/v6.5/source/net/ipv4/tcp_output.c#L206

        let window = std::cmp::min(window, max_window_size);

        if window == 0 {
            return 0;
        }

        let shift = window
            .ilog2()
            .saturating_sub(Self::MAX_SCALE_SHIFT as u32 + 1);
        let shift = std::cmp::min(shift, Self::MAX_SCALE_SHIFT as u32);

        shift.try_into().unwrap()
    }

    /// Disable window scaling. This will ensure that a window scale is not sent in a SYN packet.
    pub fn disable(&mut self) {
        // tried to disable window scaling after sending the SYN
        assert!(!self.sent_syn);
        self.disabled = true;
    }

    /// A SYN packet was sent with the given window scale.
    pub fn sent_syn(&mut self, window_scale: Option<u8>) {
        // why did we send the SYN twice?
        assert!(!self.sent_syn);

        // if it was disabled, we shouldn't have sent a window scale in the SYN
        if self.disabled {
            assert!(window_scale.is_none());
        }

        if self.received_syn && self.send_window_scale_shift.is_none() {
            // RFC 7323 1.3.:
            // > Furthermore, the Window Scale option will be sent in a <SYN,ACK> segment only if
            // > the corresponding option was received in the initial <SYN> segment.
            assert!(window_scale.is_none());
        }

        if let Some(window_scale) = window_scale {
            // RFC 7323 2.3.:
            // > Since the max window is 2^S (where S is the scaling shift count) times at most 2^16
            // > - 1 (the maximum unscaled window), the maximum window is guaranteed to be < 2^30 if
            // > S <= 14.  Thus, the shift count MUST be limited to 14 (which allows windows of 2^30
            // > = 1 GiB).
            assert!(window_scale <= Self::MAX_SCALE_SHIFT);
        }

        self.sent_syn = true;
        self.recv_window_scale_shift = window_scale;
    }

    /// A SYN packet was received with the given window scale.
    pub fn received_syn(&mut self, mut window_scale: Option<u8>) {
        // why did we receive the SYN twice?
        assert!(!self.received_syn);

        if let Some(ref mut window_scale) = window_scale {
            // RFC 7323 2.3.:
            // > If a Window Scale option is received with a shift.cnt value larger than 14, the TCP
            // > SHOULD log the error but MUST use 14 instead of the specified value.
            if *window_scale > Self::MAX_SCALE_SHIFT {
                *window_scale = Self::MAX_SCALE_SHIFT;
            }
        }

        self.received_syn = true;
        self.send_window_scale_shift = window_scale;
    }

    /// Checks if it's valid for an outward SYN packet to contain a window scale option.
    pub fn can_send_window_scale(&self) -> bool {
        // can't send if window scaling has been disabled, or if we've already received a SYN packet
        // that did not have window scaling enabled
        !(self.disabled || (self.received_syn && self.send_window_scale_shift.is_none()))
    }

    /// Has window scaling been configured? This does *not* mean that it's enabled. If this returns
    /// true, then `recv_window_scale_shift()` and `send_window_scale_shift()` should not panic.
    pub fn is_configured(&self) -> bool {
        self.disabled || (self.sent_syn && self.received_syn)
    }

    fn recv_shift(&self) -> u8 {
        if self.disabled {
            return 0;
        }

        if self.send_window_scale_shift.is_some() && self.recv_window_scale_shift.is_some() {
            self.recv_window_scale_shift.unwrap()
        } else {
            0
        }
    }

    fn send_shift(&self) -> u8 {
        if self.disabled {
            return 0;
        }

        if self.send_window_scale_shift.is_some() && self.recv_window_scale_shift.is_some() {
            self.send_window_scale_shift.unwrap()
        } else {
            0
        }
    }

    /// The right bit-shift to apply to the receive buffer's window size when sending a packet.
    pub fn recv_window_scale_shift(&self) -> u8 {
        // we shouldn't be trying to get the receive window scale before we've fully configured
        // window scaling
        assert!(self.is_configured());

        self.recv_shift()
    }

    /// The left bit-shift to apply to the send buffer's window size when receiving a packet.
    pub fn send_window_scale_shift(&self) -> u8 {
        // we shouldn't be trying to get the send window scale before we've fully configured window
        // scaling
        assert!(self.is_configured());

        self.send_shift()
    }

    pub fn recv_window_max(&self) -> u32 {
        (u16::MAX as u32) << self.recv_shift()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_scale_shift_for_max_window() {
        // the maximum possible window allowed by tcp
        const MAX_WINDOW_SIZE: u32 = (u16::MAX as u32) << 14;

        fn test(window: u32, expected: u8) {
            let rv = WindowScaling::scale_shift_for_max_window(window);

            assert_eq!(rv, expected);

            // Check that applying the window scale bit-shift does result in a value that fits
            // within a u16. We can't check this if the provided window size is larger than
            // `MAX_WINDOW_SIZE`, since we use a max bit shift value of 14.
            if window <= MAX_WINDOW_SIZE {
                assert!(window >> rv <= u16::MAX as u32);
            }
        }

        const U16_MAX: u32 = u16::MAX as u32;

        test(0, 0);
        test(1, 0);

        test(U16_MAX, 0);
        test(U16_MAX + 1, 1);

        test(2 * U16_MAX, 1);
        test(2 * U16_MAX + 1, 1);
        test(2 * (U16_MAX + 1), 2);

        test(4 * U16_MAX, 2);
        test(4 * U16_MAX + 1, 2);
        test(4 * U16_MAX + 2, 2);
        test(4 * (U16_MAX + 1), 3);

        test(MAX_WINDOW_SIZE / 2, 13);
        test(MAX_WINDOW_SIZE / 2 + 1, 13);
        test(MAX_WINDOW_SIZE / 2 + 14, 13);
        test(MAX_WINDOW_SIZE / 2 + 2_u32.pow(13) - 1, 13);
        test(MAX_WINDOW_SIZE / 2 + 2_u32.pow(13), 14);

        test(MAX_WINDOW_SIZE - 1, 14);
        test(MAX_WINDOW_SIZE, 14);
        test(MAX_WINDOW_SIZE + 1, 14);
        test(MAX_WINDOW_SIZE + 2_u32.pow(14), 14);
        test(MAX_WINDOW_SIZE + 2_u32.pow(14) + 1, 14);

        test(u32::MAX, 14);
    }
}
