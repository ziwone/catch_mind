#pragma once
// Word bank data — edit this file to add/remove categories and words.
// Each category should have at least 10 words; ~40 is recommended.
// BODY category removed intentionally.

#include <string>
#include <vector>
#include <unordered_map>

inline void loadWordBank(std::unordered_map<std::string, std::vector<std::string>> &wb) {

    wb["ANIMAL"] = {
        "CAT", "DOG", "RABBIT", "TIGER", "ELEPHANT", "LION", "BEAR",
        "PENGUIN", "DOLPHIN", "FOX", "HORSE", "SHEEP", "MONKEY", "FROG",
        "EAGLE", "SHARK", "TURTLE", "SNAKE",
        "ZEBRA", "KANGAROO", "PANDA", "OCTOPUS", "GIRAFFE", "CROCODILE"
    };

    wb["FRUIT"] = {
        "APPLE", "BANANA", "GRAPE", "ORANGE", "MELON", "PEACH", "STRAWBERRY",
        "WATERMELON", "PINEAPPLE", "CHERRY", "MANGO", "LEMON", "KIWI",
        "BLUEBERRY", "TOMATO", "AVOCADO", "LIME"
    };

    wb["FOOD"] = {
        "PIZZA", "HAMBURGER", "SUSHI", "RAMEN", "HOTDOG", "SANDWICH",
        "DONUT", "CAKE", "COOKIE", "POPCORN", "ICECREAM",
        "STEAK", "SALAD", "WAFFLE", "PANCAKE",
        "BAGEL", "NOODLE", "TOAST", "TACO", "EGG", "BREAD"
    };

    wb["OBJECT"] = {
        "CAR", "PHONE", "CHAIR", "CLOCK", "BOOK", "CUP", "UMBRELLA", "CAMERA",
        "GUITAR", "BICYCLE", "AIRPLANE", "ROCKET", "LAMP", "SCISSORS", "HAMMER",
        "BALLOON", "CANDLE", "CROWN", "TROPHY",
        "BACKPACK", "HELMET", "KEY", "KNIFE", "RING"
    };

    wb["NATURE"] = {
        "MOUNTAIN", "RIVER", "OCEAN", "VOLCANO", "FOREST", "DESERT", "RAINBOW",
        "TORNADO", "CLOUD", "WATERFALL",
        "LAKE", "BEACH", "TREE", "FLOWER", "SUN"
    };

    wb["SPORT"] = {
        "SOCCER", "BASKETBALL", "BASEBALL", "TENNIS", "SWIMMING", "GOLF",
        "BOXING", "SKIING", "SURFING", "ARCHERY",
        "BOWLING", "CYCLING", "HOCKEY",
        "BADMINTON", "RUNNING", "SKATING", "FISHING"
    };

    wb["PLACE"] = {
        "SCHOOL", "HOSPITAL", "AIRPORT", "MUSEUM", "LIBRARY",
        "RESTAURANT", "HOTEL", "CASTLE", "FARM",
        "MARKET", "BRIDGE", "BAKERY",
        "ZOO", "PYRAMID", "IGLOO",
        "PARK", "BANK", "CHURCH", "BEACH"
    };
}
