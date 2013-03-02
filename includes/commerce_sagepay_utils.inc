<?php

  /**
   * @file commerce_sagepay_utils.inc
   * Common utilities shared by all Integration methods.
   */

  /**
   * Helper function to use Commerce to fetch the Card names
   */
  function _commerce_sagepay_all_card_names() {

    return array(
      'visa' => 'Visa',
      'uke' => 'Visa Electron',
      'mc' => 'Mastercard',
      'amex' => 'American Express',
      'delta' => 'Delta',
      'dc' => 'Diners Club',
      'jcb' => 'JCB',
      'laser' => 'Laser',
      'maestro' => 'Maestro',
    );

  }

  /**
   * The card types that SagePay supports.
   */
  function _commerce_sagepay_all_card_types() {
    return array_keys(_commerce_sagepay_all_card_names());
  }

  function _commerce_sagepay_populate_card_names($cards) {
    $card_array = array();
    $names = _commerce_sagepay_all_card_names();
    foreach (array_values($cards) as $c) {
      if ($c != '0') $card_array[$c] = $names[$c];
    }

    return $card_array;
  }


  /**
   * Some configurations have a name line where others have a first name and last name.
   * This function checks both scenarios and returns a consistent name array.
   * @param $name_line
   * @param $first_name
   * @param $last_name
   * @return array
   */
  function _commerce_sagepay_format_customer_name($name_line, $first_name, $last_name) {

    // If we have been supplied with a $name_line variable, split it by space
    // and make the assumption that the last word is the last name and all other words
    // are the first name.
    $name = array();

    if (isset($name_line)) {
      $split_name = explode(' ', $name_line);

      $last_name = array_pop($split_name);
      foreach ($split_name as $n) {
        $first_name .= $n;
        $first_name .= ' ';
      }

      // Allow for a situation where only one name was entered on the name line
      if ($first_name == '') $first_name = 'not set';
    }

    $name['first_name'] = trim($first_name);
    $name['last_name'] = trim($last_name);

    return $name;
  }

  /**
   * Encrypt a string ready to send to SagePay using encryption key.
   * @param $string
   * @param $key
   * @return string
   */
  function _commerce_sagepay_encrypt_and_encode($string, $key) {

    //** AES encryption, CBC blocking with PKCS5 padding then HEX encoding - DEFAULT **

    //** add PKCS5 padding to the text to be encypted
    $string = _commerce_sagepay_addPKCS5Padding($string);

    //** perform encryption with PHP's MCRYPT module
    $crypt = mcrypt_encrypt(MCRYPT_RIJNDAEL_128, $key, $string, MCRYPT_MODE_CBC, $key);

    //** perform hex encoding and return
    return "@" . bin2hex($crypt);

  }

  /**
   * Decode a returned string from SagePay.
   * @param $str_in
   * @param $str_encryption_password
   * @return string
   */
  function _commerce_sagepay_decode_and_decrypt($str_in, $str_encryption_password) {


    //** HEX decoding then AES decryption, CBC blocking with PKCS5 padding - DEFAULT **

    //** use initialization vector (IV) set from $str_encryption_password
    $str_iv = $str_encryption_password;

    //** remove the first char which is @ to flag this is AES encrypted
    $str_in = substr($str_in, 1);

    //** HEX decoding
    $str_in = pack('H*', $str_in);

    //** perform decryption with PHP's MCRYPT module
    return _commerce_sagepay_removePKCS5Padding(mcrypt_decrypt(MCRYPT_RIJNDAEL_128, $str_encryption_password, $str_in, MCRYPT_MODE_CBC, $str_iv));

  }

  /**
   * Remove PKCS5 Padding from a string.
   * @param $decrypted
   * @return string
   */
  function _commerce_sagepay_removePKCS5Padding($decrypted) {
    $pad_char = ord($decrypted[strlen($decrypted) - 1]);
    return substr($decrypted, 0, -$pad_char);
  }


  /**
   * PHP's mcrypt does not have built in PKCS5 Padding, so we use this
   * @param $input
   * @return string
   */
  function _commerce_sagepay_addPKCS5Padding($input) {
    $blocksize = 16;
    $padding = "";

    // Pad input to an even block size boundary
    $padlength = $blocksize - (strlen($input) % $blocksize);
    for ($i = 1; $i <= $padlength; $i++) {
      $padding .= chr($padlength);
    }

    return $input . $padding;
  }

  /**
   * Decode SagePay response.
   * @param $scrambled
   * @return string
   */
  function _commerce_sagepay_base64Decode($scrambled) {
    $corrected = str_replace(" ", "+", $scrambled);
    $output = base64_decode($corrected);
    return $output;
  }

  /**
   * Validate a string against allowed parameters in the SagePay Protocol.
   * @param $text
   * @param $max_length
   * @param string $type
   * @return mixed
   */
  function _commerce_sagepay_validate_input($text, $max_length, $type = SAGEPAY_VALIDATE_ADDRESS) {

    // Trim text to max permitted length.
    if (strlen($text) > $max_length) {
      $text = substr($text, 0, $length);
    }

    // Strip unwanted characters.
    $accents = '/&([A-Za-z]{1,2})(grave|acute|circ|cedil|uml|lig);/';
    $text_encoded = htmlentities($text, ENT_NOQUOTES, 'UTF-8');
    $text = preg_replace($accents, '$1', $text_encoded);
    switch ($type) {
      case SAGEPAY_VALIDATE_NAME:
        $pat = '/[^(A-Za-z& \/\.\'\-)]*/';
        break;
      case SAGEPAY_VALIDATE_ADDRESS:
        $pat = '/[^(A-Za-z0-9&:, \/\.\'\+\-\{\})]*/';
        break;
      case SAGEPAY_VALIDATE_POSTCODE:
        $pat = '/[^(A-Za-z0-9 \-)]*/';
        break;
      case SAGEPAY_VALIDATE_EMAIL:
        //@todo could check for a valid email here, but need to know what to do if it isn't valid.
        return $text;
    }

    return preg_replace($pat, '', $text);
  }

  /**
   * For testing purposes, SagePay needs one of a set of test card numbers and the following test data
   * Billing Address1 = '88'
   * Billing Postcode = '412'
   * Expiry date = Sometime in the future
   * CV2 = 123
   * Visa test card number: 4929000000006
   * Mastercard test card number: 5404000000000001
   */
  function _commerce_sagepay_substitute_for_test_data(&$transaction) {
    watchdog('commerce_sagepay_direct', 'Processing transaction in test mode so overwriting entered data with test data', array(), WATCHDOG_WARNING);
    $transaction['BillingAddress1'] = '88';
    $transaction['BillingPostcode'] = '412';
    $transaction['CV2'] = '123';
    $transaction['CardNumber'] = '5404000000000001';
    $transaction['ExpiryDate'] = '1230';
    $transaction['CardType'] = 'MC';
  }

  /**
   * Convert a data array to a single string ready to post.
   * @param $data
   * @return string
   */
  function _commerce_sagepay_array_to_post($data) {

    $post = '';
    foreach ($data as $name => $value) {
      $post .= urlencode($name) . '=' . urlencode($value) . '&';
    }

    // chop off the last &
    $post = substr($post, 0, -1);

    return $post;
  }

  /**
   * Remove unwanted characters from strings.
   * @param $text
   * @param $type
   * @return string
   */
function _commerce_sagepay_clean_input($text, $type) {

//  if ($type == "Number") {
//    $chars = "0123456789.";
//  }
//  elseif ($type == "VendorTxCode") {
//    $chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.";
//  }
//  else {
//    $chars = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-.,'/{}@():?-_&£$=%~<>*+\"";
//  }
//
//  $pattern = "/[^".preg_quote($chars, "/")."]/";
//  $clean_text = preg_replace($pattern, "", $text);

  $clean_text = check_plain($text);
  return $clean_text;

}