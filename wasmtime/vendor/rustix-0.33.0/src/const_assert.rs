/// A simple `assert` macro that works in `const fn`, for use until the
/// standard `assert` macro works in `const fn`.
#[allow(unused_macros)]
macro_rules! const_assert {
    ($x:expr) => {
        let b: bool = $x;
        let _ = [()][!b as usize];
    };
}

#[test]
fn test_const_assert() {
    const_assert!(true);
}
